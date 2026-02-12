#!/usr/bin/env python3
# udp_server_pi.py
import threading
import time
from dataclasses import dataclass
from typing import Callable
import signal

import logger as log
from config_options import load_config_options
from my_Rosmaster import Rosmaster, apply_actions, initialize_rosmaster, read_state
from protocol import Actions, CMD_STRUCT, States, parse_cmd_pkt, prepare_state_pkt, print_actions, print_states
from queue_recorder import QueueRecorder, MyQueue
from udp import UDPSockets, UDPRxSockets, UDPTxSockets

DEFAULT_PRINT_INTERVAL_S = 1.0
DEFAULT_RECORDER_QUEUE_MAX = 5000
DEFAULT_IDLE_SLEEP_S = 0.002


running = False
def _stop(*args):
    global running
    log.warn("Recived Stop SIGNAL!!")
    running = False

signal.signal(signal.SIGINT, _stop)
signal.signal(signal.SIGTERM, _stop)
  
        
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
    info_port: int
    recorder_dir: str
    recorder_prefix: str
    idle_sleep_s: float


def build_udp_server_config(cfg) -> UdpServerConfig:
    return UdpServerConfig(
        rx_ip=cfg.udp.local_ip or "127.0.0.1",
        rx_port=cfg.udp.cmd_port,
        tx_ip=cfg.udp.local_ip or "127.0.0.1",
        tx_port=cfg.udp.state_port,
        info_port=cfg.udp.info_port,
        tx_hz=cfg.timing.state_hz,
        recorder_dir=cfg.recorder.recorder_dir,
        recorder_prefix=cfg.recorder.recorder_prefix,
        idle_sleep_s=DEFAULT_IDLE_SLEEP_S,
    )



class Server:
    def __init__(self, handle_state: Callable[[], tuple[bytes, States]],
    handle_cmd: Callable[[bytes], Actions],
    handle_timeout_if_needed: Callable[[], None],
    server_cfg: UdpServerConfig,
):
        self.terminate_event = threading.Event()
        self.tx_hz = server_cfg.tx_hz
        self.idle_sleep_s = server_cfg.idle_sleep_s
        self.handle_state = handle_state
        self.handle_cmd = handle_cmd
        self.handle_timeout_if_needed = handle_timeout_if_needed
        self.start_time = time.time()
        
        rx_ip = server_cfg.rx_ip
        rx_port = server_cfg.rx_port
        tx_ip = server_cfg.tx_ip
        tx_port = server_cfg.tx_port
        self.info_port = server_cfg.info_port
        self.udp_rx = UDPRxSockets(ip=rx_ip, port=rx_port)
        self.udp_tx = UDPTxSockets(ip=tx_ip, port=tx_port)
        log.info(f"UDP RX bound to {rx_ip}:{rx_port}, TX to {tx_ip}:{tx_port} at {self.tx_hz:.2f} Hz")

        self.tx_que = MyQueue(maxsize=DEFAULT_RECORDER_QUEUE_MAX)
        self.rx_que = MyQueue(maxsize=DEFAULT_RECORDER_QUEUE_MAX)
        self.recorder = QueueRecorder(server_cfg.recorder_dir, self.tx_que, self.rx_que, prefix=server_cfg.recorder_prefix)
        self.state = "READY"
        
    def rx_loop(self) -> None:
        try:
            while not self.terminate_event.is_set():
                pkt = self.udp_rx.try_recv_pkt(timeout=0.1, pct_size=CMD_STRUCT.size)
                if pkt is None:
                    continue
                try:
                    cmd = self.handle_cmd(pkt)
                    self.rx_que.put_nowait(cmd)
                except Exception as exc:
                    log.warn(f"handle_cmd failed: {exc}")

        except Exception as exc:
            log.error(f"RX stopped: {exc}")
            self.terminate_event.set()

    def tx_loop(self) -> None:
        dt = 1.0 / self.tx_hz
        next_time = time.perf_counter()
        try:
            while not self.terminate_event.is_set():
                now = time.perf_counter()
                if now < next_time:
                    time.sleep(next_time - now)
                    continue
                next_time += dt

                pkt, state = self.handle_state()
                self.udp_tx.send_pkt(pkt)
                self.tx_que.put_nowait(state)

        except Exception as exc:
            log.error(f"Tx loop stopped: {exc}")
            self.terminate_event.set()

    def cmd_timeout_loop(self) -> None:
        try:
            while not self.terminate_event.is_set():
                self.handle_timeout_if_needed()
                time.sleep(1)
        except Exception as exc:
            log.error(f"CMD timeout loop stopped: {exc}")
            self.terminate_event.set()
    
    def info_server_thread(self):
        import socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        host = "127.0.0.1"
        sock.bind((host, self.info_port))
        sock.settimeout(1.0)
        log.info(f"Status UDP listening on {host}:{self.info_port}")

        try:
            while not self.terminate_event.is_set():
                
                try:
                    data, addr = sock.recvfrom(256)
                except socket.timeout:
                    continue
                except Exception as e:
                    lof.info("Status server error:", e)
                    continue

                msg = data.decode(errors="ignore").strip().lower()
                if msg in ("status", "status?", "ping", "health"):
                    reply = self.info().encode()
                elif msg in ("stop", "exit"):
                    log.info("stop message recived over the INFO port")
                    self.terminate_event.set()
                    self.state = "EXITING"
                    reply = self.info().encode()
                else:
                    reply = b"err=unknown_command use=status"

                try:
                    sock.sendto(reply, addr)
                except Exception:
                    pass
            
        except Exception as exc:
            log.error(f"Info loop stopped: {exc}")
            self.terminate_event.set()
    
    def info(self) -> str:
        now = time.time()
        uptime = now - self.start_time
        # one-line, parse-friendly
        return (f"state={self.state} uptime_s={uptime:.1f}")
    
    def run(self):
        global running
        running = True
        self.recorder.start()
        self.t_rx = threading.Thread(target=self.rx_loop, daemon=True)
        self.t_tx = threading.Thread(target=self.tx_loop, daemon=True)
        self.t_info = threading.Thread(target=self.info_server_thread, daemon=True)
        self.t_timeout = threading.Thread(target=self.cmd_timeout_loop, daemon=True)
        self.state = "RUNNING"
        self.t_rx.start()
        self.t_tx.start()
        self.t_info.start()
        self.t_timeout.start()
    
        while not self.terminate_event.is_set() and running:
            time.sleep(self.idle_sleep_s)
        self.close()
        
    def close(self) -> None:
        log.info("closing the server")
        self.terminate_event.set()
        self.t_rx.join()
        self.t_tx.join()
        self.t_info.join()
        self.t_timeout.join()
        self.recorder.stop()
        self.recorder.join()
        
    def __del__(self) -> None:
        self.close()

    def __exit__(self) -> None:
        self.close()

class HandleStates:
    def __init__(self, bot: Rosmaster,  print_interval_s: float):
        self.last_printed = 0.0
        self.seq = 0
        self.print_interval_s = print_interval_s
        self.bot = bot
    
    def handle(self) -> tuple[bytes, States]:
        self.seq += 1
        state = read_state(self.bot)
        state.seq = self.seq
        t_mono = time.perf_counter()
        pkt = prepare_state_pkt(state, t_mono)
        self.last_printed = _maybe_print(self.last_printed, lambda: print_states(state), self.print_interval_s)
        return pkt, state
    
class HandleActions:
    def __init__(self, bot: Rosmaster,  print_interval_s: float,  cmd_timeout_s: float):
        self.last_printed = 0.0
        self.print_interval_s = print_interval_s
        self.cmd_timeout_s = cmd_timeout_s
        self.bot = bot
        self.last_cmd_time = 0.0

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
        if age > self.cmd_timeout_s:
            try:
                actions = Actions()
                apply_actions(self.bot, actions)
            except Exception as exc:
                log.warn(f"handle_cmd failed on timeout: {exc}")

               
def main() -> None:
    cfg = load_config_options()
    _configure_logging(cfg)
    server_cfg = build_udp_server_config(cfg)

    bot = initialize_rosmaster(cfg.rosmaster.linux_port, debug=False)
    handle_state = HandleStates(bot, print_interval_s=DEFAULT_PRINT_INTERVAL_S)
    handle_actions = HandleActions(bot, print_interval_s=DEFAULT_PRINT_INTERVAL_S, cmd_timeout_s=cfg.timing.cmd_timeout_s)
    
    log.info("UDP server starting")
    server = Server(
            handle_state.handle,
            handle_actions.handle,
            handle_actions.handle_timeout_if_needed,
            server_cfg)
    try:
        server.run()
    except Exception as exc:
        log.warn(f"running server failed: {exc}. Shutting down")
    finally:
        server.close()
        log.close_logger()

if __name__ == "__main__":
    main()
