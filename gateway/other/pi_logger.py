#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
ROS Robot Control Board (Rosmaster) UDP Gateway + 100 Hz Logger

- Reads IMU (accel+gyro), attitude (roll/pitch/yaw), encoders (2) from Rosmaster at 100 Hz
- Sends latest state as a fixed-size binary UDP packet to localhost (or any IP)
- Receives command packets over UDP (m1..m4 + optional one-shot beep)
- Logs to CSV with local time + epoch + monotonic


"""
import argparse
import time
import struct
from datetime import datetime
from typing import Optional
from Rosmaster_Lib import Rosmaster
from udp import UDPSockets
from state_logger import StateLogger
from config_loader import *
from protocol import *

def main():
    cfg = load_config()
    rate_hz = float(cfg.get("timing", "rate_hz", fallback=DEFAULT_RATE))
    dt = 1.0 / rate_hz
    
    log_dir = cfg.get("logging", "log_dir", fallback=DEFAULT_OUTDIR)
    prefix = cfg.get("logging", "prefix", fallback=DEFAULT_PREFIX)
    duration = cfg.get("timing", "duration", fallback=DEFAULT_DURATION)
    
    # --- Connect to board ---
    com_port = cfg.get("rosmaster", "port", fallback=DEFAULT_COM_PORT)
    bot = initialize_rosmaster(com_port)

    # --- Setup UDP sockets ---
    tx_ip=cfg.get("udp", "local_ip", fallback=DEFAULT_STATE_IP)
    tx_port=cfg.getint("udp", "state_port", fallback=DEFAULT_STATE_PORT)
    rx_port=cfg.getint("udp", "cmd_port", fallback=DEFAULT_CMD_PORT)
    udp = UDPSockets(tx_ip=pc_ip, tx_port=tx_port, rx_ip=rx_ip, rx_port=30002)
    
    # Give it a moment to start filling data
    starting_beep(bot)
    actions = Actions()
    start_mono = time.perf_counter()
    next_tick = start_mono
    n = 0
    last_cmd_seq = None
    state_seq = 0
    with StateLogger(log_dir, prefix) as dlogger:
        try:
            while True:
                now_mono = time.perf_counter()
                if now_mono < next_tick:
                    # While waiting, still accept commands (no busy loop)
                    timeout = max(0.0, next_tick - now_mono)
                    pkt = udp.rx.try_recv_pkt(timeout, CMD_STRUCT.size)
                    if pkt is not None:
                        actions = parse_cmd_pkt(pkt)
                        print(f"[INFO] Received CMD seq={actions.seq} m1={actions.motors.m1} m2={actions.motors.m2} m3={actions.motors.m3} m4={actions.motors.m4} beep_ms={actions.beep_ms}")
                    continue

                # Schedule next tick (prevents drift)
                next_tick += dt
                
                # Non-blocking command poll (in case packets arrive exactly at tick time)
                pkt = udp.rx.recv_pkt_non_blocking(CMD_STRUCT.size)
                if pkt is not None:
                    actions = parse_cmd_pkt(pkt)
                    print(f"[INFO] Received CMD seq={actions.seq} m1={actions.motors.m1} m2={actions.motors.m2} m3={actions.motors.m3} m4={actions.motors.m4} beep_ms={actions.beep_ms}")
                
                # --- Read latest sensor values (updated by receive thread) ---
                state = read_state(bot)

                # --- Send outputs ---
                if actions.seq != last_cmd_seq and actions.seq >= 0:
                    last_cmd_seq = actions.seq
                    apply_actions(bot, actions)
                    print(f"[INFO] Received CMD seq={actions.seq} m1={actions.motors.m1} m2={actions.motors.m2} m3={actions.motors.m3} m4={actions.motors.m4} beep_ms={actions.beep_ms}")

                
                # --- Log state to CSV ---
                dlogger.log_state(state, actions, now_mono)

                # --- Send STATE packet over UDP ---
                state_seq += 1
                state.seq = state_seq 
                pkt = prepare_state_pkt(state, now_mono)
                udp.tx.send_pkt(pkt)

                n += 1
                if n % int(rate_hz) == 0:
                    elapsed = time.perf_counter() - start_mono
                    print(f"[INFO] {n} samples logged | elapsed={elapsed:.1f}s")

                # Stop condition
                if duration > 0 and (time.perf_counter() - start_mono) >= duration:
                    break

        except KeyboardInterrupt:
            print("\n[INFO] Stopped by user (Ctrl+C).")

        finally:
            # Safety stop motors on exit
            try:
                bot.set_motor(0, 0, 0, 0)
            except Exception:
                pass

    print("[INFO] Done.")

if __name__ == "__main__":
    main()
