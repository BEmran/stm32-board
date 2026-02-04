#!/usr/bin/env python3

import argparse
import socket
import threading
import time

from config_loader import (
    DEFAULT_TCP_HOST,
    DEFAULT_TCP_PORT,
    load_config,
)
from protocol import Actions, STATE_STRUCT, parse_state_pkt, prepare_cmd_pkt
from other.tcp import recv_exact

def rx_loop(sock: socket.socket, print_hz: float, stop: threading.Event) -> None:
    last_print = 0.0
    min_dt = 1.0 / print_hz if print_hz > 0 else 0.0
    try:
        while not stop.is_set():
            pkt = recv_exact(sock, STATE_STRUCT.size)
            t_mono, state = parse_state_pkt(pkt)
            if min_dt <= 0.0:
                continue
            now = time.time()
            if now - last_print >= min_dt:
                last_print = now
                print(
                    f"[TEST] STATE seq={state.seq} t_mono={t_mono:.6f} "
                    f"roll={state.ang.roll:.2f} pitch={state.ang.pitch:.2f} yaw={state.ang.yaw:.2f} "
                    f"enc1={state.enc.e1} enc2={state.enc.e2} enc3={state.enc.e3} enc4={state.enc.e4}"
                )
    except ConnectionError:
        print("[TEST] RX stopped: server closed")
        stop.set()

def tx_loop(
    sock: socket.socket,
    rate_hz: float,
    motor_step: int,
    motor_limit: int,
    beep_period: int,
    stop: threading.Event,
) -> None:
    dt = 1.0 / rate_hz if rate_hz > 0 else 0.1
    seq = 0
    m_val = 0
    direction = 1
    try:
        while not stop.is_set():
            seq += 1

            m_val += direction * motor_step
            if m_val >= motor_limit:
                direction = -1
            elif m_val <= -motor_limit:
                direction = 1

            actions = Actions()
            actions.seq = seq
            actions.motors.m1 = m_val
            actions.motors.m2 = -m_val
            actions.motors.m3 = 0
            actions.motors.m4 = 0
            if beep_period > 0 and seq % beep_period == 0:
                actions.beep_ms = 80

            sock.sendall(prepare_cmd_pkt(actions))
            time.sleep(dt)
    except OSError:
        stop.set()

def main() -> None:
    parser = argparse.ArgumentParser(description="TCP test client for Rosmaster state/cmd stream")
    parser.add_argument("--host", default=None, help="TCP host (default: config.ini)")
    parser.add_argument("--port", type=int, default=None, help="TCP port (default: config.ini)")
    parser.add_argument("--cmd-rate-hz", type=float, default=None, help="Command send rate (Hz)")
    parser.add_argument("--motor-step", type=int, default=None, help="Motor step per tick")
    parser.add_argument("--motor-limit", type=int, default=None, help="Motor absolute limit")
    parser.add_argument("--beep-period", type=int, default=None, help="Beep every N commands (0=off)")
    parser.add_argument("--print-hz", type=float, default=1.0, help="State print rate (Hz)")
    parser.add_argument("--no-tx", action="store_true", help="Disable command transmit")
    args = parser.parse_args()

    cfg = load_config()
    host = args.host or cfg.get("tcp", "host", fallback=DEFAULT_TCP_HOST)
    port = args.port or cfg.getint("tcp", "port", fallback=DEFAULT_TCP_PORT)
    cmd_rate_hz = (
        args.cmd_rate_hz
        if args.cmd_rate_hz is not None
        else cfg.getfloat("test_client", "cmd_rate_hz", fallback=10.0)
    )
    motor_step = (
        args.motor_step
        if args.motor_step is not None
        else cfg.getint("test_client", "motor_step", fallback=10)
    )
    motor_limit = (
        args.motor_limit
        if args.motor_limit is not None
        else cfg.getint("test_client", "motor_limit", fallback=50)
    )
    beep_period = (
        args.beep_period
        if args.beep_period is not None
        else cfg.getint("test_client", "beep_period", fallback=30)
    )

    print(f"[TEST] Connecting to {host}:{port}")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    print("[TEST] Connected")

    stop = threading.Event()
    rx_thread = threading.Thread(target=rx_loop, args=(sock, args.print_hz, stop), daemon=True)
    rx_thread.start()

    try:
        if args.no_tx:
            while not stop.is_set():
                time.sleep(0.2)
        else:
            tx_loop(sock, cmd_rate_hz, motor_step, motor_limit, beep_period, stop)
    except KeyboardInterrupt:
        stop.set()
    finally:
        sock.close()
        print("[TEST] Closed")

if __name__ == "__main__":
    main()
