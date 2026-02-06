"""
    Packet formats (little-endian):

STATE (56 bytes):
  < I d  f f f  f f f  f f f  f f f  i i i i f
    seq, t_mono, ax,ay,az, gx,gy,gz, mx,my,mz r,p,y, enc1,enc2,enc3,enc4,battery

CMD (12 bytes):
  < I H H H H H B
    seq, m1,m2,m3,m4, beep_ms, flags

"""
import struct
from dataclasses import dataclass, field

STATE_STRUCT = struct.Struct("<Ifffffffffffffiiiif")   # 76 bytes
CMD_STRUCT   = struct.Struct("<IHHHHHB")               # 15 bytes

@dataclass
class Point3d:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

@dataclass
class Encoders:
    e1: float = 0.0; e2: float = 0.0; e3: float = 0.0; e4: float = 0.0

@dataclass
class Motors:
    m1: int = 0; m2: int = 0; m3: int = 0; m4: int = 0

@dataclass
class Angles:
    roll: float = 0.0; pitch: float = 0.0; yaw: float = 0.0

@dataclass
class IMU:
    acc: Point3d = field(default_factory=Point3d)
    gyro: Point3d = field(default_factory=Point3d)
    mag: Point3d = field(default_factory=Point3d)

@dataclass
class State:
    seq: int = 0
    imu: IMU = field(default_factory=IMU)
    ang: Angles = field(default_factory=Angles)
    enc: Encoders = field(default_factory=Encoders)
    battery: float = 0.0

@dataclass
class Actions:
    seq: int = 0
    motors: Motors = field(default_factory=Motors)
    beep_ms: int = 0
    flags: int = 0


def prepare_state_pkt(state: State, now_mono: float = 0.0) -> bytes:
    """Prepare STATE_STRUCT binary packet from dataclasss."""
    pkt = STATE_STRUCT.pack(
        int(state.seq),
        now_mono,
        float(state.imu.acc.x), float(state.imu.acc.y), float(state.imu.acc.z),
        float(state.imu.gyro.x), float(state.imu.gyro.y), float(state.imu.gyro.z),
        float(state.imu.mag.x), float(state.imu.mag.y), float(state.imu.mag.z),
        float(state.ang.roll), float(state.ang.pitch), float(state.ang.yaw),
        int(state.enc.e1), int(state.enc.e2), int(state.enc.e3), int(state.enc.e4), float(state.battery),
    )
    return pkt

def prepare_cmd_pkt(actions: Actions) -> bytes:
    """Prepare CMD_STRUCT binary packet from Actions dataclass."""
    pkt = CMD_STRUCT.pack(
        int(actions.seq),
        int(actions.motors.m1), int(actions.motors.m2), int(actions.motors.m3), int(actions.motors.m4),
        actions.beep_ms, actions.flags,
    )
    return pkt

def parse_cmd_pkt(pkt: bytes) -> Actions:
    """Parse CMD_STRUCT binary state into Actions dataclass."""
    unpacked = CMD_STRUCT.unpack(pkt)
    actions = Actions()
    actions.seq = int(unpacked[0])
    actions.motors.m1 = int(unpacked[1])
    actions.motors.m2 = int(unpacked[2])
    actions.motors.m3 = int(unpacked[3])
    actions.motors.m4 = int(unpacked[4])
    actions.beep_ms = int(unpacked[5])
    actions.flags = unpacked[6]
    return actions

def parse_state_pkt(pkt: bytes) -> State:
    """Parse STATE_STRUCT binary state into State dataclass."""
    unpacked = STATE_STRUCT.unpack(pkt)
    state = State()
    state.seq = int(unpacked[0])
    t_mono = float(unpacked[1])
    state.imu.acc.x = float(unpacked[2])
    state.imu.acc.y = float(unpacked[3])
    state.imu.acc.z = float(unpacked[4])
    state.imu.gyro.x = float(unpacked[5])
    state.imu.gyro.y = float(unpacked[6])
    state.imu.gyro.z = float(unpacked[7])
    state.imu.mag.x = float(unpacked[8])
    state.imu.mag.y = float(unpacked[9])
    state.imu.mag.z = float(unpacked[10])
    state.ang.roll = float(unpacked[11])
    state.ang.pitch = float(unpacked[12])
    state.ang.yaw = float(unpacked[13])
    state.enc.e1 = int(unpacked[14])
    state.enc.e2 = int(unpacked[15])
    state.enc.e3 = int(unpacked[16])
    state.enc.e4 = int(unpacked[17])
    state.enc.e4 = int(unpacked[17])
    state.battery = float(unpacked[18])
    return t_mono, state

def print_states(state: State):
    print(f'seq={state.seq:8} '
          f'ax={state.imu.acc.x:+7.2f} ay={state.imu.acc.y:+7.2f} az={state.imu.acc.z:+7.2f} '
          f'gx={state.imu.gyro.x:+7.2f} gy={state.imu.gyro.y:+7.2f} gz={state.imu.gyro.z:+7.2f} '
          f'mx={state.imu.mag.x:+7.2f} my={state.imu.mag.y:+7.2f} mz={state.imu.mag.z:+7.2f} '
          f'roll={state.ang.roll:+7.2f} pitch={state.ang.pitch:+7.2f} yaw={state.ang.yaw:+7.2f} '
          f'enc1={state.enc.e1:4} enc2={state.enc.e2:4} enc3={state.enc.e3:4} enc4={state.enc.e4:4} '
          f'battery={state.battery:+7.2f}')

def print_actions(actions: Actions):
    print(f'seq={actions.seq:8} '
          f'm1={actions.motors.m1:4} m2={actions.motors.m2:4} m3={actions.motors.m3:4} m4={actions.motors.m4:4} '
          f'beep_ms={actions.beep_ms:4} flags={actions.flags:4}')