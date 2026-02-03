import socket, time
from protocol import * 
if "__main__" == __name__:
    PC_IP = "127.0.0.1"
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    
    actions = Actions()
    actions.motors.m1 = 100
    actions.motors.m2 = 100
    actions.motors.m3 = 100
    actions.motors.m4 = 100
            
    for i in range(10):
        pkt = prepare_cmd_pkt(actions)
        s.sendto(pkt, (PC_IP, 20002))
        time.sleep(0.2)
    print("sent")