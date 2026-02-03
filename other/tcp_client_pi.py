#!/usr/bin/env python3

import argparse
import socket
import threading
import time

from config_options import load_config_options
from protocol import Actions, STATE_STRUCT, parse_state_pkt, prepare_cmd_pkt
from tcp import recv_exact


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
                    f"[PC] STATE seq={state.seq} t_mono={t_mono:.6f} "
                    f"roll={state.ang.roll:.2f} pitch={state.ang.pitch:.2f} yaw={state.ang.yaw:.2f} "
                    f"enc1={state.enc.e1} enc2={state.enc.e2} enc3={state.enc.e3} enc4={state.enc.e4}"
                )
    except ConnectionError:
        print("[PC] RX stopped: server closed")
        stop.set()


def tx_loop(sock: socket.socket, rate_hz: float, stop: threading.Event) -> None:
    dt = 1.0 / rate_hz if rate_hz > 0 else 0.1
    seq = 0
    try:
        while not stop.is_set():
            seq += 1
            actions = Actions()
            actions.seq = seq
            sock.sendall(prepare_cmd_pkt(actions))
            time.sleep(dt)
    except OSError:
        stop.set()


def main() -> None:
    parser = argparse.ArgumentParser(description="TCP client for local-only server")
    parser.add_argument("--print-hz", type=float, default=1.0, help="State print rate (Hz, 0=off)")
    parser.add_argument("--cmd-rate-hz", type=float, default=10.0, help="Command send rate (Hz)")
    parser.add_argument("--no-tx", action="store_true", help="Disable command transmit")
    args = parser.parse_args()

    cfg = load_config_options()
    host = cfg.udp.local_ip or "127.0.0.1"
    port = cfg.tcp.port

    print(f"[PC] Connecting to {host}:{port}")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    print("[PC] Connected")

    stop = threading.Event()
    rx_thread = threading.Thread(target=rx_loop, args=(sock, args.print_hz, stop), daemon=True)
    rx_thread.start()

    try:
        if args.no_tx:
            while not stop.is_set():
                time.sleep(0.2)
        else:
            tx_loop(sock, args.cmd_rate_hz, stop)
    except KeyboardInterrupt:
        stop.set()
    finally:
        sock.close()
        print("[PC] Closed")


if __name__ == "__main__":
    main()
