#!/usr/bin/env python3
# udp_client_pi.py
import argparse
import threading
import time
from pathlib import Path

from config_options import load_config_options
from protocol import STATE_STRUCT, parse_state_pkt

# Allow importing udp.py from ./other without making it a package
_OTHER_DIR = Path(__file__).resolve().parent / "other"
if str(_OTHER_DIR) not in __import__("sys").path:
    __import__("sys").path.append(str(_OTHER_DIR))
from udp import UDPRxSockets  # noqa: E402


def rx_loop(rx: UDPRxSockets, print_hz: float, stop: threading.Event) -> None:
    last_print = 0.0
    min_dt = 1.0 / print_hz if print_hz > 0 else 0.0
    while not stop.is_set():
        pkt = rx.try_recv_pkt(timeout=0.1, pct_size=STATE_STRUCT.size)
        if not pkt:
            continue
        t_mono, state = parse_state_pkt(pkt)
        if min_dt <= 0.0:
            continue
        now = time.time()
        if now - last_print >= min_dt:
            last_print = now
            print(
                f"[UDP] STATE seq={state.seq} t_mono={t_mono:.6f} "
                f"roll={state.ang.roll:.2f} pitch={state.ang.pitch:.2f} yaw={state.ang.yaw:.2f} "
                f"enc1={state.enc.e1} enc2={state.enc.e2} enc3={state.enc.e3} enc4={state.enc.e4}"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="UDP client to read Rosmaster state packets")
    parser.add_argument("--print-hz", type=float, default=1.0, help="State print rate (Hz, 0=off)")
    args = parser.parse_args()

    cfg = load_config_options()
    rx_ip = cfg.udp.pc_ip or cfg.udp.local_ip
    rx_port = cfg.udp.state_port
    rx = UDPRxSockets(ip=rx_ip, port=rx_port)
    print(f"[UDP] Listening on {rx_ip}:{rx_port}")

    stop = threading.Event()
    t = threading.Thread(target=rx_loop, args=(rx, args.print_hz, stop), daemon=True)
    t.start()

    try:
        while not stop.is_set():
            time.sleep(0.2)
    except KeyboardInterrupt:
        stop.set()


if __name__ == "__main__":
    main()
