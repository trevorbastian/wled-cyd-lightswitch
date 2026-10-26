"""Validation rules for config.json, shared by the build (embed_config.py) and cyd.py."""

MAX_LIGHTS = 4
SSID_MAX = 32
PASSWORD_MAX = 63
NAME_MAX = 24
HOST_MAX = 63
TOUCH_KEYS = ["x_min", "x_max", "y_min", "y_max"]


class ConfigError(ValueError):
    pass


def _check_keys(obj, where, allowed):
    if not isinstance(obj, dict):
        raise ConfigError(f"{where} must be an object")
    unknown = set(obj) - set(allowed)
    if unknown:
        raise ConfigError(f"unknown key(s) in {where}: {', '.join(sorted(unknown))} "
                          f"(allowed: {', '.join(allowed)})")


def check_str(value, where, min_len, max_len):
    if not isinstance(value, str):
        raise ConfigError(f"{where} must be a string")
    n = len(value.encode("utf-8"))
    if not min_len <= n <= max_len:
        raise ConfigError(f"{where} must be {min_len}-{max_len} bytes long (got {n})")


def check_password(password, where="wifi.password"):
    check_str(password, where, 0, PASSWORD_MAX)
    if 0 < len(password) < 8:
        raise ConfigError(f"{where} must be empty (open network) or at least 8 characters")


def validate(cfg):
    """Raises ConfigError describing the first problem found."""
    _check_keys(cfg, "top level", ["wifi", "lights", "touch"])

    if "wifi" in cfg:
        wifi = cfg["wifi"]
        _check_keys(wifi, "wifi", ["ssid", "password"])
        check_str(wifi.get("ssid", ""), "wifi.ssid", 1, SSID_MAX)
        check_password(wifi.get("password", ""))

    if "lights" in cfg:
        lights = cfg["lights"]
        if not isinstance(lights, list):
            raise ConfigError("lights must be a list")
        if len(lights) > MAX_LIGHTS:
            raise ConfigError(f"at most {MAX_LIGHTS} lights are supported (got {len(lights)})")
        for i, light in enumerate(lights):
            _check_keys(light, f"lights[{i}]", ["name", "host"])
            check_str(light.get("name"), f"lights[{i}].name", 1, NAME_MAX)
            check_str(light.get("host"), f"lights[{i}].host", 1, HOST_MAX)

    if "touch" in cfg:
        touch = cfg["touch"]
        _check_keys(touch, "touch", TOUCH_KEYS)
        for k in TOUCH_KEYS:
            v = touch.get(k)
            if not isinstance(v, int) or isinstance(v, bool) or not 0 <= v <= 4095:
                raise ConfigError(f"touch.{k} must be a whole number from 0 to 4095")
        if abs(touch["x_max"] - touch["x_min"]) < 1000 or abs(touch["y_max"] - touch["y_min"]) < 1000:
            raise ConfigError("touch x/y min and max look wrong (they should be at least 1000 apart)")
