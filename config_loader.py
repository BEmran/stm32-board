import configparser
from pathlib import Path

DEFAULT_TCP_HOST = "0.0.0.0"
DEFAULT_TCP_PORT = 30001

DEFAULT_LINUX_COM_PORT = "/dev/ttyUSB0"
DEFAULT_STATE_RATE = 100.0  # Hz
DEFAULT_CMD_TIMEOUT_S = 0.5  # seconds


DEFAULT_DURATION = 0.0  # seconds (0 = run forever)
DEFAULT_OUTDIR = "./logs"
DEFAULT_PREFIX = "run"
DEFAULT_STATE_IP = "127.0.0.1"
DEFAULT_STATE_PORT = 20001 
DEFAULT_CMD_IP = "127.0.0.1"
DEFAULT_CMD_PORT = 20002

CFG_PATH = Path(__file__).resolve().parent / "config.ini"

def load_config(verbose: bool = False) -> configparser.ConfigParser:
    if not CFG_PATH.exists():
        raise FileNotFoundError(f"Missing config file: {CFG_PATH}")

    cfg = configparser.ConfigParser(interpolation=None)

    # IMPORTANT: utf-8-sig handles Windows BOM correctly
    with CFG_PATH.open("r", encoding="utf-8-sig") as f:
        cfg.read_file(f)

    if verbose:
        print(f"[CFG] Loaded from: {CFG_PATH}")
        print(f"[CFG] Sections: {cfg.sections()}")
        for sec in cfg.sections():
            print(f"[CFG] [{sec}] keys: {list(cfg[sec].keys())}")

    return cfg

def require(cfg: configparser.ConfigParser, section: str, key: str) -> str:
    """Get a required key; raises a clear error if missing."""
    if not cfg.has_section(section):
        raise KeyError(f"Missing section [{section}] in {CFG_PATH}. Sections found: {cfg.sections()}")
    if not cfg.has_option(section, key):
        raise KeyError(f"Missing key '{key}' in section [{section}] in {CFG_PATH}. Keys found: {list(cfg[section].keys())}")
    return cfg.get(section, key)


if __name__ == "__main__":
    config = load_config(True)
    print("Configuration loaded:")
    for section in config.sections():
        print(f"[{section}]")
        for key, value in config.items(section):
            print(f"{key} = {value}")
