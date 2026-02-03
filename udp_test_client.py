#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from udp import UDPRxSockets, UDPTxSockets
import threading, time, struct
from config_loader import *
from protocol import *

lock = threading.Lock()
stop = False

last_state = State()
last_state_time = 0.0
def rx_target(udp: UDPRxSockets):
    global stop, last_state, last_state_time
    last_print = 0.0
    while not stop:
        pkt = udp.try_recv_pkt(timeout=0.1, pct_size=STATE_STRUCT.size)
        if pkt:
            now = time.time()
            t_mono, state = parse_state_pkt(pkt)
            
            with lock:
                last_state_time = now
                last_state = state
                
            # Print at ~10 Hz
            if now - last_print >= 1.0:
                last_print = now
                print(f"STATE seq={state.seq:6d}",
                      f"t_mono={t_mono:.6f}",
                     f"poll={state.ang.roll:.2f}",
                     f"pitch={state.ang.pitch:.2f}",
                     f"yaw={state.ang.yaw:.2f}",
                     f"ax={state.imu.acc.x:.2f}",
                     f"ay={state.imu.acc.y:.2f}",
                     f"az={state.imu.acc.z:.2f}",
                     f"gx={state.imu.gyro.x:.2f}",
                     f"gy={state.imu.gyro.y:.2f}",
                     f"gz={state.imu.gyro.z:.2f}",
                     f"enc1={state.enc.e1}",
                     f"enc2={state.enc.e2}",
                     f"enc3={state.enc.e3}",
                     f"enc4={state.enc.e4}")


def create_cmd(m_val, cmd_seq) -> Actions:
    actions = Actions()
    actions.seq = cmd_seq
    actions.motors.m1 = m_val
    actions.motors.m2 = -m_val
    actions.motors.m3 = 100
    actions.motors.m4 = -1

    # One-shot beep every 30 commands (~3 sec)
    if cmd_seq % 30 == 0:
        actions.beep_ms = 80
    else:
        actions.beep_ms = 0
    return actions

def tx_loop(udp: UDPTxSockets, dt: float):
    
    global stop
    cmd_seq = 0
    last_send = time.time()

    # Example command pattern: ramp m1/m2, keep m3/m4=0
    m_val = 0
    direction = 1

    while not stop:

        # Send command at ~10 Hz
        now = time.time()
        if now - last_send >= dt:
            last_send = now
            cmd_seq += 1
                    
            # simple ramp
            m_val += direction * 10
            if m_val >= 50:
                direction = -1
            elif m_val <= -50:
                direction = 1
        
            actions = create_cmd(m_val, cmd_seq)
            pkt = prepare_cmd_pkt(actions)
            udp.send_pkt(pkt)

if __name__ == "__main__":
    try:
        cfg = load_config()
        tx_ip=cfg.get("udp", "local_ip", fallback=DEFAULT_STATE_IP)
        tx_port=cfg.getint("udp", "state_port", fallback=DEFAULT_STATE_PORT)
        rx_ip=cfg.get("udp", "local_ip", fallback=DEFAULT_CMD_IP)
        rx_port=cfg.getint("udp", "cmd_port", fallback=DEFAULT_CMD_PORT)
        udp_rx = UDPRxSockets(ip=rx_ip, port=tx_port)

        udp_tx = UDPTxSockets(ip=tx_ip, port=rx_port)
        rx_thread = threading.Thread(target=rx_target, args=(udp_rx,), daemon=True)
        rx_thread.start()
        dt = 1 / float(cfg.get("timing", "rate_hz", fallback=DEFAULT_RATE))
        tx_loop(udp_tx, dt)
        
    except KeyboardInterrupt:
        stop = True
        rx_thread.join()
        print("\n[TEST] Exiting.")
