
import csv
import os
from datetime import datetime
from typing import Optional
from protocol import Actions, State

ActionsHeader = ["t_epoch_s", "t_mono_s", "m1", "m2", "m3", "m4", "beep_ms", "flags"]
StateHeader = ["t_epoch_s", "t_mono_s", "ax", "ay", "az","gx","gy","gz","mx","my","mz","roll_deg","pitch_deg","yaw_deg","enc1","enc2","enc3","enc4",]

def actions_to_dict(time: float, now_mono:float, actions:Actions):
    row = {
        "t_epoch_s": f"{time:.6f}",
        "t_mono_s": f"{now_mono:.6f}",
        "m1": int(actions.motors.m1),
        "m2": int(actions.motors.m2),
        "m3": int(actions.motors.m3),
        "m4": int(actions.motors.m4),
        "beep_ms": int(actions.beep_ms),
        "flags": int(actions.flags),
    }
    return row

def state_to_dict(time: float, now_mono:float, state:State):
    row = {
        "t_epoch_s": f"{time:.6f}",
        "t_mono_s": f"{now_mono:.6f}",
        "ax": f"{state.imu.acc.x:.6f}",
        "ay": f"{state.imu.acc.y:.6f}",
        "az": f"{state.imu.acc.z:.6f}",
        "gx": f"{state.imu.gyro.x:.6f}",
        "gy": f"{state.imu.gyro.y:.6f}",
        "gz": f"{state.imu.gyro.z:.6f}",
        "mx": f"{state.imu.mag.x:.6f}",
        "my": f"{state.imu.mag.y:.6f}",
        "mz": f"{state.imu.mag.z:.6f}",
        "roll_deg": f"{state.ang.roll:.6f}",
        "pitch_deg": f"{state.ang.pitch:.6f}",
        "yaw_deg": f"{state.ang.yaw:.6f}",
        "enc1": int(state.enc.e1),
        "enc2": int(state.enc.e2),
        "enc3": int(state.enc.e3),
        "enc4": int(state.enc.e4),
    }
    return row

class CSVLogger:
    def __init__(self, logdir: str, prefix: str, header: list):
        self.csv_path = self.create_csv(logdir, prefix)
        self.file = None
        self.state_writer = None
        self.cmd_writer = None
        self.header = header
        
    def create_csv(self, logdir, prefix: str):
        stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        os.makedirs(logdir, exist_ok=True)
        csv_path = os.path.join(logdir, f"{prefix}_{stamp}.csv")
        print(f"[INFO] Logging STATE to: {csv_path}")
        return csv_path
    
    def write_header(self):
        self.writer = csv.DictWriter(self.file, fieldnames=self.header)
        self.writer.writeheader()

    def log(self, raw_dict: dict):
        # Use line-buffered writes for safer logs (still efficient at 100 Hz)
        self.writer.writerow(raw_dict)

    def __enter__(self):
        """Sets up the 'with' block context."""
        self.file = open(self.csv_path, "w", newline="", buffering=1)
        self.write_header()
        return self  # This is what 'as logger' receives
    
    def close_log(self):
        if hasattr(self, 'file') and not self.file.closed:
            self.file.close()

    # Automatically runs when the object is garbage collected
    def __del__(self):
        self.close_log()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close_log()