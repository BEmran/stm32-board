# pi_server.py
import time
import threading
import socket

from tcp import *
from protocol import *
from my_Rosmaster import *
from config_loader import *
from queue_logger import *

def handle_client(conn: socket.socket, addr, bot: Rosmaster, state_hz: float, cmd_timeout_s: float) -> None:
    print(f"[PI] Client connected: {addr}")

    set_low_latency(conn)
    set_buffers(conn)

    # Shared command + timing
    lock = threading.Lock()
    last_cmd_time = time.time()
    last_cmd = Actions()
    state_q = queue.Queue(maxsize=5000)
    cmd_q   = queue.Queue(maxsize=5000)
    running = True
    
    def rx_cmd_loop():
        nonlocal running, last_cmd_time, last_cmd
        last_printed = 0.0
        try:
            while running:
                pkt = recv_exact(conn, CMD_STRUCT.size)
                last_cmd = parse_cmd_pkt(pkt)
                with lock:
                    last_cmd_time = time.time()
                    print("[PI] No CMD received")
                    apply_actions(bot, last_cmd)
                    
                t_wall = time.time()
                t_mono = time.perf_counter()
                try:
                    cmd_q.put_nowait((t_wall, t_mono, last_cmd))
                except queue.Full:
                    pass

                if t_mono - last_printed > 1.0:
                    print_actions(last_cmd)
                    last_printed = t_mono
                    
        except Exception as e:
            print(f"[PI] RX stopped: {e}")
            running = False
            

    def tx_state_loop(state_hz: float, cmd_timeout_s: float):
        nonlocal running, last_cmd_time, last_cmd
        dt = 1.0 / state_hz
        next_time = time.perf_counter()
        last_printed = 0.0
        try:
            while running:
                now = time.perf_counter()
                if now < next_time:
                    time.sleep(next_time - now)
                    continue
                next_time += dt

                # Optional safety hook
                with lock:
                    age = time.time() - last_cmd_time
                    if age > cmd_timeout_s:
                        print("[PI] No CMD received for %.2f s, stopping motors" % age)
                        apply_actions(bot, Actions())  # stop motors

                states = read_state(bot)
                pkt = prepare_state_pkt(states)
                conn.sendall(pkt)
                
                t_wall = time.time()
                t_mono = time.perf_counter()
                try:
                    state_q.put_nowait((t_wall, t_mono, states))
                except queue.Full:
                    pass

                if now - last_printed > 1.0:
                    print_states(states)
                    last_printed = now

        except Exception as e:
            print(f"[PI] TX stopped: {e}")
            running = False

            
    t_rx = threading.Thread(target=rx_cmd_loop, daemon=True)
    t_tx = threading.Thread(target=tx_state_loop, args=(state_hz, cmd_timeout_s, ), daemon=True)
    t_rx.start()
    t_tx.start()
    logger  = QueueLogger("./logs", state_q, cmd_q)
    logger.start()

    while running:
        time.sleep(0.2)

    try:
        conn.close()
    except Exception:
        pass
    logger.stop()
    logger.join()
    print(f"[PI] Client disconnected: {addr}")


def main():
    
    cfg = load_config()
    state_hz = cfg.getfloat("timing", "state_hz", fallback=DEFAULT_STATE_RATE)
    
    log_dir = cfg.get("logging", "log_dir", fallback=DEFAULT_OUTDIR)
    prefix = cfg.get("logging", "prefix", fallback=DEFAULT_PREFIX)
    
    # --- Connect to board ---
    com_port = cfg.get("rosmaster", "linux_port", fallback=DEFAULT_LINUX_COM_PORT)
    bot = initialize_rosmaster(com_port, debug=True)

    # --- Setup TCP sockets ---
    port=cfg.getint("tcp", "port", fallback=DEFAULT_TCP_PORT)
    host=cfg.get("tcp", "host", fallback=DEFAULT_TCP_HOST)
    server = TcpServer(host, port, backlog=1)
    server.open()
    print(f"[PI] Listening on {host}:{port} (one client at a time)")
    
    cmd_timeout_s = cfg.getfloat("timing", "cmd_timeout_s", fallback=DEFAULT_CMD_TIMEOUT_S)

    while True:
        conn, addr = server.accept()
        handle_client(conn, addr, bot, state_hz, cmd_timeout_s)
        print("[PI] Waiting for next client...\n")

if __name__ == "__main__":
    main()
