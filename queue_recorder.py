# logger.py
import time, queue, threading
from pathlib import Path
from csv_logger import *

class QueueLogger(threading.Thread):
    def __init__(self, logdir: str, state_q, cmd_q):
        super().__init__(daemon=True)
        self.logdir = logdir
        self.state_q = state_q
        self.cmd_q = cmd_q
        self._stop = False
    
    def stop(self):
        self._stop = True

    def run(self):
        with CSVLogger(logdir=self.logdir, prefix="state", header=StateHeader) as state_logger, \
             CSVLogger(logdir=self.logdir, prefix="cmd", header=ActionsHeader) as cmd_logger:
            # Drain loop
            while not self._stop:
                drained = False
                drained |= self._drain(state_logger, self.state_q)
                drained |= self._drain(cmd_logger, self.cmd_q)
                if not drained:
                    time.sleep(0.01)  # back off when idle

    def _drain(self, logger, queue):
        wrote = False
        if not queue.empty():
            item = queue.get_nowait()
            time, now_mono, data = item
            if isinstance(data, Actions):
                data_dict = actions_to_dict(time, now_mono, data)
                logger.log(data_dict)
            elif isinstance(data, State):
                data_dict = state_to_dict(time, now_mono, data)
                logger.log(data_dict)
            wrote = True
        return wrote

