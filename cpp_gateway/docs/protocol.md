# Gateway TCP Protocol (Framed Binary)

This gateway exposes **two TCP ports**:

- **STATE port**: pushes `MSG_STATE` frames at `tcp_hz` (best-effort).
- **CMD port**: receives command frames (`MSG_CMD`, `MSG_SETPOINT`, `MSG_CONFIG`) and can also answer `MSG_STATS_REQ`.

All frames use a **3-byte header** followed by a payload.

---

## Endianness / Numeric Types (IMPORTANT)

For external clients (Python / Matlab / etc.):

- All multi-byte integers are **little-endian**.
- Floats are **IEEE-754 binary32** (float32), sent as the raw 32-bit bit pattern in **little-endian**.

Do **not** rely on C/C++ struct packing; decode fields explicitly.

---

## Frame Format

### Header (3 bytes)

| Byte | Name | Type | Notes |
|---:|---|---|---|
| 0 | `type` | `uint8` | message type |
| 1 | `ver` | `uint8` | protocol version (=1) |
| 2 | `len` | `uint8` | payload length in bytes (0..255) |

### Message Types

| Type | Name | Direction | Payload |
|---:|---|---|---|
| 1 | `MSG_STATE` | gateway → client | 76 bytes |
| 2 | `MSG_CMD` | client → gateway | 14 bytes (legacy) |
| 3 | `MSG_SETPOINT` | client → gateway | 21 bytes |
| 4 | `MSG_CONFIG` | client → gateway | 12 bytes |
| 5 | `MSG_STATS_REQ` | client → gateway | 0 bytes |
| 6 | `MSG_STATS_RESP` | gateway → client | 48 bytes |

---

## Payload Layouts

### `MSG_STATE` payload (76 bytes)

Offset | Field | Type
---:|---|---
0 | `seq` | `uint32`
4 | `t_mono_s` | `float32`
8 | `acc.x` | `float32`
12 | `acc.y` | `float32`
16 | `acc.z` | `float32`
20 | `gyro.x` | `float32`
24 | `gyro.y` | `float32`
28 | `gyro.z` | `float32`
32 | `mag.x` | `float32`
36 | `mag.y` | `float32`
40 | `mag.z` | `float32`
44 | `roll` | `float32`
48 | `pitch` | `float32`
52 | `yaw` | `float32`
56 | `enc1` | `int32`
60 | `enc2` | `int32`
64 | `enc3` | `int32`
68 | `enc4` | `int32`
72 | `battery_voltage` | `float32`

### `MSG_CMD` payload (14 bytes) — legacy

Offset | Field | Type
---:|---|---
0 | `seq` | `uint32`
4 | `m1` | `int16`
6 | `m2` | `int16`
8 | `m3` | `int16`
10 | `m4` | `int16`
12 | `beep_ms` | `uint8`
13 | `flags` | `uint8`

### `MSG_SETPOINT` payload (21 bytes)

Offset | Field | Type
---:|---|---
0 | `seq` | `uint32`
4 | `sp0` | `float32`
8 | `sp1` | `float32`
12 | `sp2` | `float32`
16 | `sp3` | `float32`
20 | `flags` | `uint8`

### `MSG_CONFIG` payload (12 bytes)

Offset | Field | Type
---:|---|---
0 | `seq` | `uint32`
4 | `key` | `uint8`
5 | `u8` | `uint8`
6 | `u16` | `uint16`
8 | `u32` | `uint32`

#### CONFIG keys
Key | Meaning | Value field
---:|---|---
1 | usb_hz | `u16` (clamped 1..2000)
2 | tcp_hz | `u16` (clamped 1..2000)
3 | ctrl_hz | `u16` (clamped 1..2000)
4 | cmd_timeout_ms | `u16` (clamped 10..5000)
5 | usb_timeout_mode | `u8` (0=ENFORCE, 1=DISABLE)
6 | log_rotate_mb | `u16`
7 | log_rotate_keep | `u16`
10 | flag_event_mask | `u8`
20 | control_mode | `u8`
30 | ctrl_thread_priority | `u16` interpreted as signed int16 (Linux FIFO priority)

### `MSG_STATS_RESP` payload (48 bytes)

Offset | Field | Type
---:|---|---
0 | `seq` | `uint32`
4 | `uptime_ms` | `uint32`
8 | `usb_hz` | `float32`
12 | `tcp_hz` | `float32`
16 | `ctrl_hz` | `float32`
20 | `drops_state` | `uint32`
24 | `drops_cmd` | `uint32`
28 | `drops_event` | `uint32`
32 | `drops_sys_event` | `uint32`
36 | `tcp_frames_bad` | `uint32`
40 | `serial_errors` | `uint32`
44 | reserved | `uint32`

---

## Python decoding example (STATE)

```python
import socket, struct

HDR_FMT = "<BBB"          # type, ver, len
STATE_FMT = "<I f " + "f"*15 + "i"*4 + "f"  # matches the table above
STATE_LEN = 76

s = socket.create_connection(("pi-ip", 30001))
while True:
    hdr = s.recv(3)
    if len(hdr) < 3: break
    msg_type, ver, ln = struct.unpack(HDR_FMT, hdr)
    payload = b""
    while len(payload) < ln:
        payload += s.recv(ln - len(payload))
    if msg_type != 1 or ln != STATE_LEN:  # MSG_STATE
        continue
    values = struct.unpack(STATE_FMT, payload)
    seq = values[0]
    t_mono = values[1]
    # ...
```

## Matlab decoding hint

Use `fread(sock, N, 'uint8')` then `typecast(uint8bytes, 'single')` / `typecast(...,'uint32')` and swap bytes if needed (should not if you use little-endian typecast on little-endian host).

