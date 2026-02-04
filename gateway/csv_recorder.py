#csv_recorder.py
import csv
import os
from datetime import datetime
from typing import Dict, Iterable

import logger as log
from protocol import Actions, State

ActionsHeader = ["t_epoch_s", "t_mono_s", "m1", "m2", "m3", "m4", "beep_ms", "flags"]
StateHeader = [
    "t_epoch_s",
    "t_mono_s",
    "ax",
    "ay",
    "az",
    "gx",
    "gy",
    "gz",
    "mx",
    "my",
    "mz",
    "roll_deg",
    "pitch_deg",
    "yaw_deg",
    "enc1",
    "enc2",
    "enc3",
    "enc4",
]

def actions_to_dict(t_wall_s: float, t_mono_s: float, actions: Actions) -> Dict[str, object]:
    row = {
        "t_epoch_s": f"{t_wall_s:.6f}",
        "t_mono_s": f"{t_mono_s:.6f}",
        "m1": int(actions.motors.m1),
        "m2": int(actions.motors.m2),
        "m3": int(actions.motors.m3),
        "m4": int(actions.motors.m4),
        "beep_ms": int(actions.beep_ms),
        "flags": int(actions.flags),
    }
    return row

def state_to_dict(t_wall_s: float, t_mono_s: float, state: State) -> Dict[str, object]:
    row = {
        "t_epoch_s": f"{t_wall_s:.6f}",
        "t_mono_s": f"{t_mono_s:.6f}",
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

class CSVRecorder:
    def __init__(self, recorderdir: str, prefix: str, header: Iterable[str]):
        self.header = list(header)
        self.csv_path = self._build_path(recorderdir, prefix)
        self._file = None
        self._writer = None
        
    def _build_path(self, recorderdir: str, prefix: str) -> str:
        stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        os.makedirs(recorderdir, exist_ok=True)
        filename = f"{prefix}_{stamp}.csv" if prefix else f"{stamp}.csv"
        csv_path = os.path.join(recorderdir, filename)
        log.info(f"Recording to: {csv_path}")
        return csv_path
    
    def open(self) -> "CSVRecorder":
        if self._file is not None:
            return self
        self._file = open(self.csv_path, "w", newline="", buffering=1, encoding="utf-8")
        self._writer = csv.DictWriter(self._file, fieldnames=self.header)
        self._writer.writeheader()
        return self

    def record(self, raw_dict: Dict[str, object]) -> None:
        if self._writer is None:
            raise RuntimeError("CSVRecorder is not open. Use 'with CSVRecorder(...)' or call open().")
        self._writer.writerow(raw_dict)

    def __enter__(self):
        return self.open()
    
    def close(self) -> None:
        if self._file is not None and not self._file.closed:
            self._file.close()
        self._file = None
        self._writer = None

    def __del__(self):
        self.close()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
