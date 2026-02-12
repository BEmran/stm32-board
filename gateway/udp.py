#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import socket
from select import select
from typing import Optional

class UDPSockets():
    def __init__(self, tx_ip: str, tx_port: int, rx_ip: str, rx_port: int):
        # States TX
        self.tx = UDPTxSockets(tx_ip, tx_port)
        self.rx = UDPRxSockets(rx_ip, rx_port)
    
    def close(self):
        self.tx.close()
        self.rx.close()
        
    def __del__(self):
        self.close()

class UDPTxSockets():
    def __init__(self, ip: str, port: int):
        # States TX
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.state_addr = (ip, port)
        print(f"[INFO] Transmit -> udp://{ip}:{port}")
    
    def send_pkt(self, pkt: bytes):
        # --- Send STATE packet over UDP ---
        self.tx.sendto(pkt, self.state_addr)
        
    def close(self):
        self.tx.close()
        
    def __del__(self):
        self.close()

class UDPRxSockets():
    def __init__(self, ip: str, port: int):
        # Command RX (non-blocking)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind((ip, port))
        self.rx.setblocking(False)  
    
        print(f"[INFO] Receive <- udp://{ip}:{port}")
    
    def try_recv_pkt(self, timeout: float, pct_size: int)-> Optional[bytes]:
        # Non-blocking pkt poll (in case packets arrive exactly at tick time)
        r, _, _ = select([self.rx], [], [], timeout)
        if r:
            return self.recv_pkt_non_blocking(pct_size)
        return None
    
    def recv_pkt_non_blocking(self, pct_size: int)-> Optional[bytes]:
        # Non-blocking pkt poll (in case packets arrive exactly at tick time)
        try:
            pct, _ = self.rx.recvfrom(1024)
            if len(pct) == pct_size:
                return pct
        except BlockingIOError:
            pass
        return None
    
    def close(self):
        self.rx.close()
        
    def __del__(self):
        self.close()
