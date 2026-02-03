from dataclasses import dataclass
from typing import Optional

from config_loader import load_config

DEFAULT_TCP_HOST = "0.0.0.0"
DEFAULT_TCP_PORT = 30001
DEFAULT_LINUX_COM_PORT = "/dev/ttyUSB0"
DEFAULT_WINDOWS_COM_PORT = "17"
DEFAULT_ROS_BAUD = 115200

DEFAULT_STATE_RATE = 100.0  # Hz
DEFAULT_CMD_TIMEOUT_S = 0.5  # seconds
DEFAULT_DURATION = 0.0  # seconds (0 = run forever)

DEFAULT_RECORDER_DIR = "./records"
DEFAULT_RECORDER_PREFIX = "run"

DEFAULT_LOG_DIR = "./logs"
DEFAULT_LOG_ENABLE = True
DEFAULT_LOG_MAX_SIZE_BYTES = 1_000_000
DEFAULT_LOG_PRINT_LEVEL = "INFO"
DEFAULT_LOG_LEVEL = "DEBUG"

DEFAULT_STATE_IP = "127.0.0.1"
DEFAULT_STATE_PORT = 20001
DEFAULT_CMD_PORT = 20002

DEFAULT_CMD_MIN = -100
DEFAULT_CMD_MAX = 100
DEFAULT_FLAG_BEEP_ONCE = 1

DEFAULT_TEST_CMD_RATE_HZ = 10.0
DEFAULT_TEST_MOTOR_STEP = 10
DEFAULT_TEST_MOTOR_LIMIT = 50
DEFAULT_TEST_BEEP_PERIOD = 30


@dataclass
class TcpConfig:
    host: str
    port: int


@dataclass
class RosmasterConfig:
    linux_port: str
    windows_port: str
    baud: int


@dataclass
class RecorderConfig:
    recorder_dir: str
    recorder_prefix: str


@dataclass
class LoggingConfig:
    log_dir: str
    enable: bool
    max_size_bytes: int
    print_level: str
    log_level: str


@dataclass
class TimingConfig:
    rate_hz: float
    state_hz: float
    cmd_timeout_s: float
    duration: float


@dataclass
class UdpConfig:
    local_ip: str
    pc_ip: str
    rpi_ip: str
    state_port: int
    cmd_port: int


@dataclass
class CmdConfig:
    min: int
    max: int
    timeout: float


@dataclass
class ProtocolConfig:
    flag_beep_once: int


@dataclass
class TestClientConfig:
    cmd_rate_hz: float
    motor_step: int
    motor_limit: int
    beep_period: int


@dataclass
class ConfigOptions:
    tcp: TcpConfig
    rosmaster: RosmasterConfig
    recorder: RecorderConfig
    logging: LoggingConfig
    timing: TimingConfig
    udp: UdpConfig
    cmd: CmdConfig
    protocol: ProtocolConfig
    test_client: TestClientConfig


def _get(cfg, section: str, key: str, fallback: Optional[str] = None) -> Optional[str]:
    if cfg.has_option(section, key):
        return cfg.get(section, key)
    return fallback


def _getint(cfg, section: str, key: str, fallback: int) -> int:
    if cfg.has_option(section, key):
        return cfg.getint(section, key)
    return fallback


def _getfloat(cfg, section: str, key: str, fallback: Optional[float]) -> Optional[float]:
    if cfg.has_option(section, key):
        return cfg.getfloat(section, key)
    return fallback


def _getbool(cfg, section: str, key: str, fallback: Optional[bool]) -> Optional[bool]:
    if cfg.has_option(section, key):
        return cfg.getboolean(section, key)
    return fallback


def load_config_options() -> ConfigOptions:
    cfg = load_config()

    tcp_host = _get(cfg, "tcp", "host", DEFAULT_TCP_HOST) or DEFAULT_TCP_HOST
    tcp_port = _getint(cfg, "tcp", "port", DEFAULT_TCP_PORT)

    linux_port = _get(cfg, "rosmaster", "linux_port", None)
    if linux_port is None:
        linux_port = _get(cfg, "rosmaster", "linux_com_port", DEFAULT_LINUX_COM_PORT)
    windows_port = _get(cfg, "rosmaster", "windows_port", None)
    if windows_port is None:
        windows_port = _get(cfg, "rosmaster", "windows_com_port", DEFAULT_WINDOWS_COM_PORT)
    baud = _getint(cfg, "rosmaster", "baud", DEFAULT_ROS_BAUD)

    timing_rate_hz = _getfloat(cfg, "timing", "rate_hz", None)
    if timing_rate_hz is None:
        timing_rate_hz = _getfloat(cfg, "timming", "rate_hz", DEFAULT_STATE_RATE)
    state_hz = _getfloat(cfg, "timing", "state_hz", None)
    if state_hz is None:
        state_hz = _getfloat(cfg, "timming", "state_hz", DEFAULT_STATE_RATE)
    cmd_timeout_s = _getfloat(cfg, "timing", "cmd_timeout_s", None)
    if cmd_timeout_s is None:
        cmd_timeout_s = _getfloat(cfg, "cmd", "timeout", DEFAULT_CMD_TIMEOUT_S)
    duration = _getfloat(cfg, "timing", "duration", DEFAULT_DURATION)

    recorder_dir = _get(cfg, "recorder", "dir", DEFAULT_RECORDER_DIR) or DEFAULT_RECORDER_DIR
    recorder_prefix = _get(cfg, "recorder", "prefix", DEFAULT_RECORDER_PREFIX) or DEFAULT_RECORDER_PREFIX

    log_dir = _get(cfg, "logging", "dir", DEFAULT_LOG_DIR) or DEFAULT_LOG_DIR
    log_enable = _getbool(cfg, "logging", "enable", DEFAULT_LOG_ENABLE)
    if log_enable is None:
        log_enable = DEFAULT_LOG_ENABLE
    log_max_size_bytes = _getint(cfg, "logging", "max_size_bytes", DEFAULT_LOG_MAX_SIZE_BYTES)
    log_print_level = _get(cfg, "logging", "print_level", DEFAULT_LOG_PRINT_LEVEL) or DEFAULT_LOG_PRINT_LEVEL
    log_level = _get(cfg, "logging", "log_level", DEFAULT_LOG_LEVEL) or DEFAULT_LOG_LEVEL

    local_ip = _get(cfg, "udp", "local_ip", DEFAULT_STATE_IP) or DEFAULT_STATE_IP
    pc_ip = _get(cfg, "udp", "pc_ip", DEFAULT_STATE_IP) or DEFAULT_STATE_IP
    rpi_ip = _get(cfg, "udp", "rpi_ip", DEFAULT_STATE_IP) or DEFAULT_STATE_IP
    state_port = _getint(cfg, "udp", "state_port", DEFAULT_STATE_PORT)
    cmd_port = _getint(cfg, "udp", "cmd_port", DEFAULT_CMD_PORT)

    cmd_min = _getint(cfg, "cmd", "min", DEFAULT_CMD_MIN)
    cmd_max = _getint(cfg, "cmd", "max", DEFAULT_CMD_MAX)
    cmd_timeout = _getfloat(cfg, "cmd", "timeout", DEFAULT_CMD_TIMEOUT_S)

    flag_beep_once = _getint(cfg, "protocol", "flag_beep_once", DEFAULT_FLAG_BEEP_ONCE)

    cmd_rate_hz = _getfloat(cfg, "test_client", "cmd_rate_hz", DEFAULT_TEST_CMD_RATE_HZ)
    motor_step = _getint(cfg, "test_client", "motor_step", DEFAULT_TEST_MOTOR_STEP)
    motor_limit = _getint(cfg, "test_client", "motor_limit", DEFAULT_TEST_MOTOR_LIMIT)
    beep_period = _getint(cfg, "test_client", "beep_period", DEFAULT_TEST_BEEP_PERIOD)

    return ConfigOptions(
        tcp=TcpConfig(host=tcp_host, port=tcp_port),
        rosmaster=RosmasterConfig(linux_port=linux_port, windows_port=windows_port, baud=baud),
        recorder=RecorderConfig(recorder_dir=recorder_dir, recorder_prefix=recorder_prefix),
        logging=LoggingConfig(
            log_dir=log_dir,
            enable=log_enable,
            max_size_bytes=log_max_size_bytes,
            print_level=log_print_level,
            log_level=log_level,
        ),
        timing=TimingConfig(
            rate_hz=timing_rate_hz,
            state_hz=state_hz,
            cmd_timeout_s=cmd_timeout_s,
            duration=duration,
        ),
        udp=UdpConfig(
            local_ip=local_ip,
            pc_ip=pc_ip,
            rpi_ip=rpi_ip,
            state_port=state_port,
            cmd_port=cmd_port,
        ),
        cmd=CmdConfig(min=cmd_min, max=cmd_max, timeout=cmd_timeout),
        protocol=ProtocolConfig(flag_beep_once=flag_beep_once),
        test_client=TestClientConfig(
            cmd_rate_hz=cmd_rate_hz,
            motor_step=motor_step,
            motor_limit=motor_limit,
            beep_period=beep_period,
        ),
    )
