#!/usr/bin/env python3
# coding: utf-8
from Rosmaster_Lib import Rosmaster
from protocol import *
import time
 
WINDOWS_COM_PORT = 17 # Change this to your Windows COM port index

def initialize_rosmaster(com_port: str, debug: bool=False) -> Rosmaster:
    # --- Connect to board ---
    print(f'try to connect to ROSMASTER BOard on com port = {com_port}')
    try:
        bot = Rosmaster(com=com_port, debug=debug)
    except:
        print(f"COM port {com_port} is not available")
        exit(0)
    
    # Start receiving thread so internal variables update continuously
    bot.create_receive_threading()

    # Enable auto report from the board (forever=True keeps it streaming)
    bot.set_auto_report_state(True, forever=True)
    return bot

def read_state(bot:Rosmaster):
    # --- Read latest sensor values (updated by receive thread) ---
    state = State()
    state.imu.acc.x, state.imu.acc.y, state.imu.acc.z = bot.get_accelerometer_data()
    state.imu.gyro.x, state.imu.gyro.y, state.imu.gyro.z = bot.get_gyroscope_data()
    state.imu.mag.x, state.imu.mag.y, state.imu.mag.z = bot.get_magnetometer_data()
    state.ang.roll, state.ang.pitch, state.ang.yaw = bot.get_imu_attitude_data(ToAngle=True)
    state.enc.e1, state.enc.e2, state.enc.e3, state.enc.e4 = bot.get_motor_encoder()
    return state
    
def apply_actions(bot: Rosmaster, actions: Actions):
    # "4 PWMs" assumed as 4 motor commands.
    #bot.set_motor(actions.motors.m1, actions.motors.m2, actions.motors.m3, actions.motors.m4)
    bot.set_pwm_servo_all(actions.motors.m1, actions.motors.m2, actions.motors.m3, actions.motors.m4)
    if actions.beep_ms > 0:
        apply_beep(bot, actions.beep_ms)
        
def apply_beep(bot: Rosmaster, beep_ms: int):
    bot.set_beep(int(beep_ms))

def starting_beep(bot: Rosmaster):
    print(f"[INFO] Starting beep sequence...")
    for _ in range(3):
        apply_beep(bot, 100)
        time.sleep(0.2)

if __name__ == '__main__':
    import platform
    device = platform.system()
    if device == 'Windows':
        com = 'COM%d' % WINDOWS_COM_PORT
    elif device == 'Linux':
        com = "/dev/ttyUSB0"
    else:
        print("Unkown host OS")
        exit(0)
    
    bot = initialize_rosmaster(com, debug=True)    
    print("--------------------Open %s---------------------" % com)
    version = bot.get_version()
    print(f'Board version= {version}')
    
    try:
        while True:
            state = read_state(bot)
            print_states(state)
            time.sleep(.1)
    except KeyboardInterrupt:
        bot.close()
    
    print("Exit")
    exit(0)
