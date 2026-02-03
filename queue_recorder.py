# queue_recorder.py
import queue
import threading
import time

import logger as log
from csv_recorder import CSVRecorder, ActionsHeader, StateHeader, actions_to_dict, state_to_dict
from protocol import Actions, State


class MyQueue(queue.Queue):
    def __init__(self, maxsize: int = 0):
        super().__init__(maxsize)

    def put_nowait(self, item) -> None:
        t_wall = time.time()
        t_mono = time.perf_counter()
        try:
            super().put_nowait((t_wall, t_mono, item))
        except queue.Full:
            pass
    
    def get_nowait(self) -> tuple[float, float, object] | None:
        if self.empty():
            return None    
        return super().get_nowait()
        
class QueueRecorder(threading.Thread):
    def __init__(self, recorderdir: str, state_q, cmd_q, prefix: str = ""):
        super().__init__(daemon=True)
        self.recorderdir = recorderdir
        self.state_q = state_q
        self.cmd_q = cmd_q
        self.prefix = (prefix or "").strip()
        self._stop_event = threading.Event()
    
    def stop(self):
        self._stop_event.set()

    def run(self):
        try:
            with CSVRecorder(recorderdir=self.recorderdir, prefix=self._prefixed("state"), header=StateHeader) as state_recorder, \
                 CSVRecorder(recorderdir=self.recorderdir, prefix=self._prefixed("cmd"), header=ActionsHeader) as cmd_recorder:
                while not self._stop_event.is_set():
                    drained = False
                    drained |= self._drain(state_recorder, self.state_q)
                    drained |= self._drain(cmd_recorder, self.cmd_q)
                    if not drained:
                        time.sleep(0.01)  # back off when idle
        except Exception as exc:
            log.error(f"Recorder stopped: {exc}")
            self._stop_event.set()

    def _drain(self, recorder: CSVRecorder, q: MyQueue) -> bool:
        item = q.get_nowait()
        if item is None:
            return False
        t_wall, t_mono, data = item

        if isinstance(data, Actions):
            recorder.record(actions_to_dict(t_wall, t_mono, data))
        elif isinstance(data, State):
            recorder.record(state_to_dict(t_wall, t_mono, data))
        return True

    def _prefixed(self, name: str) -> str:
        if self.prefix:
            return f"{self.prefix}_{name}"
        return name
