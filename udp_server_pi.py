#!/usr/bin/env python3
# udp_server_pi.py
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Tuple

import logger as log
from config_options import load_config_options
from my_Rosmaster import Rosmaster, apply_actions, initialize_rosmaster, read_state
from protocol import Actions, CMD_STRUCT, State, parse_cmd_pkt, prepare_state_pkt, print_actions, print_states
from queue_recorder import QueueRecorder, MyQueue
from udp import UDPRxSockets, UDPTxSockets

DEFAULT_PRINT_INTERVAL_S = 1.0
DEFAULT_RECORDER_QUEUE_MAX = 5000
DEFAULT_IDLE_SLEEP_S = 0.002

        
def _configure_logging(cfg) -> None:
    log.set_file_logging_enabled(cfg.logging.enable)
    log.set_logs_dir(cfg.logging.log_dir)
    log.set_max_file_size(cfg.logging.max_size_bytes)
    log.set_print_level(cfg.logging.print_level)
    log.set_log_level(cfg.logging.log_level)


def _maybe_print(last_printed: float, fn, interval_s: float) -> float:
    now = time.perf_counter()
    if now - last_printed >= interval_s:
        fn()
        return now
    return last_printed


@dataclass
class UdpServerConfig:
    rx_ip: str
    rx_port: int
    tx_ip: str
    tx_port: int
    tx_hz: float
    recorder_dir: str
    recorder_prefix: str
    idle_sleep_s: float


def build_udp_server_config(cfg) -> UdpServerConfig:
    return UdpServerConfig(
        rx_ip=cfg.udp.local_ip or "127.0.0.1",
        rx_port=cfg.udp.cmd_port,
        tx_ip=cfg.udp.local_ip or "127.0.0.1",
        tx_port=cfg.udp.state_port,
        state_hz=cfg.timing.state_hz,
        recorder_dir=cfg.recorder.recorder_dir,
        recorder_prefix=cfg.recorder.recorder_prefix,
        idle_sleep_s=DEFAULT_IDLE_SLEEP_S,
    )


def run_server(
    handle_state: Callable[[], tuple[bytes, State]],
    handle_cmd: Callable[[bytes], Actions],
    handle_timeout_if_needed: Callable[[], None],
    server_cfg: UdpServerConfig,
) -> None:
    tx_hz = server_cfg.tx_hz
    recorder_dir = server_cfg.recorder_dir
    recorder_prefix = server_cfg.recorder_prefix
    rx_ip = server_cfg.rx_ip
    rx_port = server_cfg.rx_port
    tx_ip = server_cfg.tx_ip
    tx_port = server_cfg.tx_port
    idle_sleep_s = server_cfg.idle_sleep_s

    udp_rx = UDPRxSockets(ip=rx_ip, port=rx_port)
    udp_tx = UDPTxSockets(ip=tx_ip, port=tx_port)
    log.info(f"UDP RX bound to {rx_ip}:{rx_port}, TX to {tx_ip}:{tx_port} at {tx_hz:.2f} Hz")

    tx_seq = 0
    stop_event = threading.Event()
    tx_que = MyQueue(maxsize=DEFAULT_RECORDER_QUEUE_MAX)
    rx_que = MyQueue(maxsize=DEFAULT_RECORDER_QUEUE_MAX)

    def rx_loop() -> None:
        try:
            while not stop_event.is_set():
                pkt = udp_rx.try_recv_pkt(timeout=0.1, pct_size=CMD_STRUCT.size)
                if pkt is None:
                    continue
                try:
                    cmd = handle_cmd(pkt)
                    rx_que.put_nowait(cmd)
                except Exception as exc:
                    log.warn(f"handle_cmd failed: {exc}")

        except Exception as exc:
            log.error(f"RX stopped: {exc}")
            stop_event.set()

    def tx_loop() -> None:
        nonlocal tx_seq
        dt = 1.0 / tx_hz
        next_time = time.perf_counter()
        try:
            while not stop_event.is_set():
                now = time.perf_counter()
                if now < next_time:
                    time.sleep(next_time - now)
                    continue
                next_time += dt

                pkt, state = handle_state()
                udp_tx.send_pkt(pkt)
                tx_que.put_nowait(state)

        except Exception as exc:
            log.error(f"Tx loop stopped: {exc}")
            stop_event.set()

    def cmd_timeout_loop() -> None:
        if rx_timeout_s <= 0:
            return
        try:
            while not stop_event.is_set():
                handle_timeout_if_needed()
                time.sleep(0.05)
        except Exception as exc:
            log.error(f"CMD timeout loop stopped: {exc}")
            stop_event.set()

    t_rx = threading.Thread(target=rx_loop, daemon=True)
    t_tx = threading.Thread(target=tx_loop, daemon=True)
    t_timeout = threading.Thread(target=cmd_timeout_loop, daemon=True)
    t_rx.start()
    t_tx.start()
    t_timeout.start()

    recorder = QueueRecorder(recorder_dir, tx_que, rx_que, prefix=recorder_prefix)
    recorder.start()

    while not stop_event.is_set():
        time.sleep(idle_sleep_s)

    recorder.stop()
    recorder.join()


class HandleState ():
    def __init__(self, bot: Rosmaster,  print_interval_s: float):
        self.last_printed = 0.0
        self.seq = 0
        self.print_interval_s = print_interval_s
        self.bot = bot
    
    def handle(self) -> tuple[bytes, State]:
        self.seq += 1
        state = read_state(self.bot)
        state.seq = self.seq
        t_mono = time.perf_counter()
        pkt = prepare_state_pkt(state, t_mono)
        self.last_printed = _maybe_print(self.last_printed, lambda: print_states(state), self.print_interval_s)
        return pkt, state
    
class HandleActions ():
    def __init__(self, bot: Rosmaster,  print_interval_s: float,  cmd_timeout_s: float):
        self.last_printed = 0.0
        self.print_interval_s = print_interval_s
        self.cmd_timeout_s = cmd_timeout_s
        self.bot = bot
        self.last_cmd_time = time.time()
        self.last_stop_time = 0.0

    def handle(self, pkt: bytes) -> Actions:
        self.last_cmd_time = time.time()
        actions = parse_cmd_pkt(pkt)
        apply_actions(self.bot, actions)
        self.last_printed = _maybe_print(self.last_printed, lambda: print_actions(actions), self.print_interval_s)
        return actions
    
    def handle_timeout_if_needed(self):
        if self.cmd_timeout_s <=0:
            return
        age = time.time() - self.last_cmd_time
        if age > self.cmd_timeout_s and age - self.last_stop_time > self.cmd_timeout_s:
            log.warn("No CMD received for %.2f s, stopping motors" % age)
            self.last_stop_time = time.time()
            try:
                actions = Actions()
                apply_actions(self.bot, actions)
            except Exception as exc:
                log.warn(f"handle_cmd failed on timeout: {exc}")

               
def main() -> None:
    cfg = load_config_options()
    _configure_logging(cfg)
    server_cfg = build_udp_server_config(cfg)

    bot = initialize_rosmaster(cfg.rosmaster.linux_port, debug=True)
    handle_state = HandleState (bot, print_interval_s=server_cfg.print_interval_s)
    handle_actions = HandleActions (bot, print_interval_s=server_cfg.print_interval_s)
    log.info("UDP server starting")
    try:
        run_server(
            handle_state.handle,
            handle_actions.handle,
            handle_actions.handle_timeout_if_needed,
            server_cfg,
        )
    except KeyboardInterrupt:
        log.info("Shutting down")
    finally:
        log.close_logger()


if __name__ == "__main__":
    main()
