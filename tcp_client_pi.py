#!/usr/bin/env python3

import socket

PI_IP = "192.168.68.105"   # <<< CHANGE to your Pi IP
PORT = 30001

def main():
    print(f"[PC] Connecting to {PI_IP}:{PORT}")

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((PI_IP, PORT))
    print("[PC] Connected")

    try:
        while True:
            data = s.recv(1024)
            if not data:
                break
            print("[PC] RX:", data.decode("utf-8").strip())
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        print("[PC] Closed")

if __name__ == "__main__":
    main()
