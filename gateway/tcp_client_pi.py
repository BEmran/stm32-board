#!/usr/bin/env python3

import argparse
import socket
import threading
import time

from config_options import load_config_options
from protocol import Actions, STATE_STRUCT, parse_state_pkt, prepare_cmd_pkt, print_states
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
                print_states(state)
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
            pkt = prepare_cmd_pkt(actions)
            # print(pkt)
            # print(len(pkt))
            sock.sendall(pkt)
            # print("send")
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
    host = "192.168.68.101"
    cmd_port = 30001

    print(f"[PC] Connecting CMD to {host}:{cmd_port}")
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cmd_sock.connect((host, cmd_port))
    print("[PC] CMD connected")

    stop = threading.Event()
    rx_thread = threading.Thread(target=rx_loop, args=(cmd_sock, args.print_hz, stop), daemon=True)
    rx_thread.start()

    try:
        if args.no_tx:
            while not stop.is_set():
                time.sleep(0.2)
        else:
            tx_loop(cmd_sock, args.cmd_rate_hz, stop)
    except KeyboardInterrupt:
        stop.set()
    finally:
        try:
            cmd_sock.close()
        finally:
            cmd_sock.close()
        print("[PC] Closed")


if __name__ == "__main__":
    main()
