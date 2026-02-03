import socket, time

if "__main__" == __name__:
    PC_IP = "192.168.68.111"
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    for i in range(10):
        s.sendto(b"hello", (PC_IP, 20001))
        time.sleep(0.2)
    print("sent")