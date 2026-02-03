#!/usr/bin/env python3

import argparse
import socket
import threading
import time

from config_options import load_config_options
from protocol import STATE_STRUCT, parse_state_pkt
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
                    f"[LOCAL] STATE seq={state.seq} t_mono={t_mono:.6f} "
                    f"enc1={state.enc.e1} enc2={state.enc.e2} enc3={state.enc.e3} enc4={state.enc.e4}"
                )
    except ConnectionError:
        stop.set()


def main() -> None:
    parser = argparse.ArgumentParser(description="Local TCP client to keep server streaming")
    parser.add_argument("--print-hz", type=float, default=0.0, help="State print rate (Hz, 0=off)")
    args = parser.parse_args()

    cfg = load_config_options()
    host = cfg.udp.local_ip or "127.0.0.1"
    port = cfg.tcp.port

    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((host, port))
            stop = threading.Event()
            t = threading.Thread(target=rx_loop, args=(sock, args.print_hz, stop), daemon=True)
            t.start()
            while not stop.is_set():
                time.sleep(0.5)
        except KeyboardInterrupt:
            break
        except Exception:
            time.sleep(1.0)
        finally:
            try:
                sock.close()
            except Exception:
                pass


if __name__ == "__main__":
    main()
