# tcp.py
import socket
from dataclasses import dataclass
from typing import Optional, Tuple

Addr = Tuple[str, int]


@dataclass
class TcpServer:
    host: str
    port: int
    backlog: int = 1
    reuse_addr: bool = True

    def __post_init__(self):
        self._sock: Optional[socket.socket] = None

    def open(self) -> None:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if self.reuse_addr:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((self.host, self.port))
        s.listen(self.backlog)
        self._sock = s

    def accept(self, timeout_s: Optional[float] = None) -> Tuple[socket.socket, Addr]:
        if self._sock is None:
            raise RuntimeError("Server not opened. Call open() first.")
        if timeout_s is not None:
            self._sock.settimeout(timeout_s)
        conn, addr = self._sock.accept()
        return conn, (addr[0], addr[1])

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None


def set_low_latency(conn: socket.socket) -> None:
    try:
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass


def set_buffers(conn: socket.socket, rcv_bytes: int = 262144, snd_bytes: int = 262144) -> None:
    try:
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcv_bytes)
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, snd_bytes)
    except OSError:
        pass


def recv_exact(conn: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes or raise ConnectionError."""
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise ConnectionError("Client disconnected")
        data += chunk
    return data
