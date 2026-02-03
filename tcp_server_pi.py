# pi_server.py
import queue
import socket
import threading
import time
from typing import Tuple

from config_options import load_config_options
import logger as log
from my_Rosmaster import Rosmaster, apply_actions, initialize_rosmaster, read_state
from protocol import Actions, CMD_STRUCT, parse_cmd_pkt, prepare_state_pkt, print_actions, print_states
from queue_recorder import QueueRecorder
from tcp import TcpServer, recv_exact, set_buffers, set_low_latency

PRINT_INTERVAL_S = 1.0
RECORDER_QUEUE_MAX = 5000
IDLE_SLEEP_S = 0.2

def _configure_logging(cfg) -> None:
    log.set_file_logging_enabled(cfg.logging.enable)
    log.set_logs_dir(cfg.logging.log_dir)
    log.set_max_file_size(cfg.logging.max_size_bytes)
    log.set_print_level(cfg.logging.print_level)
    log.set_log_level(cfg.logging.log_level)

def _maybe_print(last_printed: float, now: float, fn) -> float:
    if now - last_printed >= PRINT_INTERVAL_S:
        fn()
        return now
    return last_printed

def handle_client(
    conn: socket.socket,
    addr: Tuple[str, int],
    bot: Rosmaster,
    state_hz: float,
    cmd_timeout_s: float,
    recorder_dir: str,
    recorder_prefix: str,
) -> None:
    log.info(f"Client connected: {addr}")

    set_low_latency(conn)
    set_buffers(conn)

    lock = threading.Lock()
    last_cmd_time = time.time()
    last_cmd = Actions()
    state_seq = 0
    stop_event = threading.Event()

    state_q = queue.Queue(maxsize=RECORDER_QUEUE_MAX)
    cmd_q = queue.Queue(maxsize=RECORDER_QUEUE_MAX)

    def rx_cmd_loop() -> None:
        nonlocal last_cmd_time, last_cmd
        last_printed = 0.0
        try:
            while not stop_event.is_set():
                pkt = recv_exact(conn, CMD_STRUCT.size)
                cmd = parse_cmd_pkt(pkt)
                with lock:
                    last_cmd_time = time.time()
                    last_cmd = cmd
                    apply_actions(bot, cmd)

                t_wall = time.time()
                t_mono = time.perf_counter()
                try:
                    cmd_q.put_nowait((t_wall, t_mono, cmd))
                except queue.Full:
                    pass

                last_printed = _maybe_print(last_printed, t_mono, lambda: print_actions(cmd))
        except Exception as exc:
            log.error(f"RX stopped: {exc}")
            stop_event.set()

    def tx_state_loop() -> None:
        nonlocal last_cmd_time, last_cmd, state_seq
        dt = 1.0 / state_hz
        next_time = time.perf_counter()
        last_printed = 0.0
        try:
            while not stop_event.is_set():
                now = time.perf_counter()
                if now < next_time:
                    time.sleep(next_time - now)
                    continue
                next_time += dt

                if cmd_timeout_s > 0:
                    with lock:
                        age = time.time() - last_cmd_time
                        if age > cmd_timeout_s:
                            log.warn("No CMD received for %.2f s, stopping motors" % age)
                            apply_actions(bot, Actions())

                state = read_state(bot)
                state_seq += 1
                state.seq = state_seq
                t_mono = time.perf_counter()
                pkt = prepare_state_pkt(state, t_mono)
                conn.sendall(pkt)

                t_wall = time.time()
                try:
                    state_q.put_nowait((t_wall, t_mono, state))
                except queue.Full:
                    pass

                last_printed = _maybe_print(last_printed, now, lambda: print_states(state))
        except Exception as exc:
            log.error(f"TX stopped: {exc}")
            stop_event.set()

    t_rx = threading.Thread(target=rx_cmd_loop, daemon=True)
    t_tx = threading.Thread(target=tx_state_loop, daemon=True)
    t_rx.start()
    t_tx.start()

    recorder = QueueRecorder(recorder_dir, state_q, cmd_q, prefix=recorder_prefix)
    recorder.start()

    while not stop_event.is_set():
        time.sleep(IDLE_SLEEP_S)

    try:
        conn.close()
    except Exception:
        pass
    recorder.stop()
    recorder.join()
    log.info(f"Client disconnected: {addr}")

def main() -> None:
    cfg = load_config_options()
    _configure_logging(cfg)

    bot = initialize_rosmaster(cfg.rosmaster.linux_port, debug=True)

    local_ip = cfg.udp.local_ip or "127.0.0.1"
    server = TcpServer(local_ip, cfg.tcp.port, backlog=1)
    server.open()
    log.info(f"Listening on {local_ip}:{cfg.tcp.port} (local only)")

    try:
        while True:
            conn, addr = server.accept()
            if addr[0] != local_ip and addr[0] != "127.0.0.1":
                log.warn(f"Rejected non-local client: {addr}")
                try:
                    conn.close()
                except Exception:
                    pass
                continue
            handle_client(
                conn,
                addr,
                bot,
                cfg.timing.state_hz,
                cfg.timing.cmd_timeout_s,
                cfg.recorder.recorder_dir,
                cfg.recorder.recorder_prefix,
            )
            log.info("Waiting for next client...")
    except KeyboardInterrupt:
        log.info("Shutting down")
    finally:
        log.close_logger()

if __name__ == "__main__":
    main()
