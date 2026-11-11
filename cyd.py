#!/usr/bin/env python3
"""Build and flash the CYD WLED remote firmware.

Works on Windows, macOS and Linux with only Python 3.8+ installed. On first use
it creates a private virtual environment (.venv) containing a pinned version of
PlatformIO, which downloads the ESP32 toolchain and libraries as needed.
"""

import argparse
import configparser
import functools
import getpass
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VENV = ROOT / ".venv"
CONFIG = ROOT / "config.json"
CONFIG_EXAMPLE = ROOT / "config.example.json"
WEB = ROOT / "web"
DIST = ROOT / "dist"

PLATFORMIO_VERSION = "6.2.0"
PIO_ENV = "cyd"
RELEASE_ENV = "release"  # same firmware, but never embeds config.json
MONITOR_BAUD = "115200"

# USB-serial chips used on ESP32-2432S028 boards
USB_SERIAL_CHIPS = {
    ("1A86", "7523"): "CH340",
    ("1A86", "55D4"): "CH9102",
    ("10C4", "EA60"): "CP210x",
}

sys.path.insert(0, str(ROOT / "scripts"))
from config_schema import (  # noqa: E402
    HOST_MAX, MAX_LIGHTS, NAME_MAX, SSID_MAX, ConfigError, check_password, check_str, validate,
)


class CommandError(Exception):
    """A failure with a message for the user (printed without a traceback)."""


def info(msg):
    print(f"==> {msg}", flush=True)


# ---------------------------------------------------------------------------
# PlatformIO environment

def venv_python():
    if os.name == "nt":
        return VENV / "Scripts" / "python.exe"
    return VENV / "bin" / "python"


def installed_platformio_version():
    if not venv_python().exists():
        return None
    result = subprocess.run(
        [str(venv_python()), "-c", "import importlib.metadata as m; print(m.version('platformio'))"],
        capture_output=True, text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def ensure_platformio(force=False):
    if force and VENV.exists():
        info("Removing existing .venv")
        shutil.rmtree(VENV)

    if not venv_python().exists():
        info(f"Creating Python virtual environment in {VENV.name}/")
        if subprocess.call([sys.executable, "-m", "venv", str(VENV)]) != 0:
            hint = ""
            if sys.platform.startswith("linux"):
                hint = "\nOn Debian/Ubuntu, install the venv module with: sudo apt install python3-venv"
            raise CommandError(f"Could not create the virtual environment.{hint}")

    version = installed_platformio_version()
    if version != PLATFORMIO_VERSION:
        info(f"Installing PlatformIO {PLATFORMIO_VERSION}" + (f" (found {version})" if version else ""))
        code = subprocess.call([
            str(venv_python()), "-m", "pip", "install", "--disable-pip-version-check", "--quiet",
            f"platformio=={PLATFORMIO_VERSION}",
        ])
        if code != 0:
            raise CommandError("Installing PlatformIO failed (see the pip output above).")


def pio(*args, capture=False, env=None):
    """Runs PlatformIO from the project directory. Raises CommandError on failure."""
    ensure_platformio()
    cmd = [str(venv_python()), "-m", "platformio", *args]
    if capture:
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            raise CommandError(f"PlatformIO failed:\n{result.stderr or result.stdout}")
        return result.stdout
    code = subprocess.call(cmd, cwd=ROOT, env=env)
    if code != 0:
        raise CommandError(f"PlatformIO exited with code {code}.")
    return None


# ---------------------------------------------------------------------------
# Serial ports

def list_ports():
    """Returns [(port, description, chip or None)], likely boards first."""
    ports = []
    for p in json.loads(pio("device", "list", "--serial", "--json-output", capture=True)):
        hwid = p.get("hwid", "").upper()
        chip = None
        if "VID:PID=" in hwid:
            vid_pid = hwid.split("VID:PID=")[1].split()[0]
            vid, _, pid = vid_pid.partition(":")
            chip = USB_SERIAL_CHIPS.get((vid, pid))
        ports.append((p["port"], p.get("description", ""), chip))
    return sorted(ports, key=lambda p: p[2] is None)


def no_board_help():
    lines = ["No ESP32 board found. Check that:",
             "  - the board is plugged in with a data cable (some USB cables only carry power)"]
    if sys.platform == "win32" or sys.platform == "darwin":
        lines.append("  - the CH340 USB-serial driver is installed: "
                     "https://www.wch-ic.com/downloads/CH341SER_EXE.html (Windows) or "
                     "https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html (macOS)")
    lines.append("Run 'cyd ports' to see all serial ports, then pass one with --port.")
    return "\n".join(lines)


def check_port_access(port):
    """On Linux, explains how to fix a missing serial port permission."""
    if not sys.platform.startswith("linux") or not os.path.exists(port):
        return
    if os.access(port, os.R_OK | os.W_OK):
        return
    import grp
    group = grp.getgrgid(os.stat(port).st_gid).gr_name
    raise CommandError(
        f"No permission to use {port}. Add yourself to the '{group}' group:\n"
        f"    sudo usermod -aG {group} $USER\n"
        "then log out and back in (or run this command via: sg "
        f"{group} -c \"./cyd ...\")."
    )


def resolve_port(requested):
    if requested:
        check_port_access(requested)
        return requested

    boards = [p for p in list_ports() if p[2]]
    if not boards:
        raise CommandError(no_board_help())
    if len(boards) > 1:
        listing = "\n".join(f"    {port}  ({chip})" for port, _, chip in boards)
        raise CommandError(f"More than one possible board found:\n{listing}\nChoose one with --port.")
    port, _, chip = boards[0]
    info(f"Using {port} ({chip})")
    check_port_access(port)
    return port


# ---------------------------------------------------------------------------
# config.json wizard

def ask(prompt, default=""):
    suffix = f" [{default}]" if default else ""
    answer = input(f"{prompt}{suffix}: ").strip()
    return answer or default


def ask_yes_no(prompt, default=False):
    answer = input(f"{prompt} [{'Y/n' if default else 'y/N'}]: ").strip().lower()
    return default if not answer else answer in ("y", "yes")


def ask_valid(prompt, check, default=""):
    while True:
        value = ask(prompt, default)
        try:
            check(value)
            return value
        except ConfigError as e:
            print(f"    {e}")


def ask_password():
    while True:
        password = getpass.getpass("WiFi password (typing is hidden; blank for an open network): ")
        try:
            check_password(password, "password")
        except ConfigError as e:
            print(f"    {e}")
            continue
        if password and getpass.getpass("Type it again to confirm: ") != password:
            print("    Passwords didn't match, try again.")
            continue
        return password


def config_wizard():
    print("Creating config.json - these become the device's default settings.")
    print("Anything you skip can be set up on the touchscreen instead.\n")
    cfg = {}

    ssid = ask_valid("WiFi network name (blank to skip)",
                     lambda v: v == "" or check_str(v, "network name", 1, SSID_MAX))
    if ssid:
        cfg["wifi"] = {"ssid": ssid, "password": ask_password()}

    print(f"\nWLED lights (up to {MAX_LIGHTS}). Use the address you open the WLED web page with.")
    lights = []
    while len(lights) < MAX_LIGHTS:
        n = len(lights) + 1
        host = ask_valid(f"Light {n} IP address or hostname (blank to finish)",
                         lambda v: v == "" or check_str(v, "address", 1, HOST_MAX))
        if not host:
            break
        name = ask_valid(f"Light {n} name", lambda v: check_str(v, "name", 1, NAME_MAX), f"Light {n}")
        lights.append({"name": name, "host": host})
    if lights:
        cfg["lights"] = lights

    example = json.loads(CONFIG_EXAMPLE.read_text(encoding="utf-8"))
    print("\nThe touchscreen is calibrated on first boot by tapping 4 crosshairs.")
    if "touch" in example and ask_yes_no("Skip that and use typical calibration values instead?"):
        cfg["touch"] = example["touch"]

    validate(cfg)
    return cfg


# ---------------------------------------------------------------------------
# Commands

def cmd_setup(args):
    ensure_platformio(force=args.force)
    info("Downloading the ESP32 platform and libraries (first time only)")
    pio("pkg", "install", "-e", PIO_ENV)
    info("Setup complete")


def cmd_config(args):
    if CONFIG.exists() and not args.force:
        raise CommandError("config.json already exists. Edit it directly, or use --force to replace it.")

    if args.copy:
        shutil.copyfile(CONFIG_EXAMPLE, CONFIG)
        info("Copied config.example.json to config.json - edit it, then run 'cyd flash'")
        return

    if not sys.stdin.isatty():
        raise CommandError("The config wizard needs an interactive terminal. Use --copy instead.")
    cfg = config_wizard()
    CONFIG.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")
    print()
    info("Wrote config.json (it's ignored by git because it contains your WiFi password)")
    info("Run 'cyd flash' to build it into the firmware")


def check_config():
    """Fails early with a clear message if config.json is invalid."""
    if not CONFIG.exists():
        info("No config.json - the device will start with on-screen setup")
        return
    try:
        validate(json.loads(CONFIG.read_text(encoding="utf-8")))
    except json.JSONDecodeError as e:
        raise CommandError(f"config.json is not valid JSON: {e}")
    except ConfigError as e:
        raise CommandError(f"config.json: {e}")


def cmd_build(args):
    check_config()
    if args.clean:
        pio("run", "-e", PIO_ENV, "-t", "clean")
    pio("run", "-e", PIO_ENV)
    info(f"Firmware built: .pio/build/{PIO_ENV}/firmware.bin")


def cmd_flash(args):
    check_config()
    port = resolve_port(args.port)
    pio("run", "-e", PIO_ENV, "-t", "upload", "--upload-port", port)
    info("Flashed successfully")
    if args.monitor:
        cmd_monitor(argparse.Namespace(port=port))


def cmd_monitor(args):
    port = resolve_port(args.port)
    info("Serial monitor - press Ctrl+C to exit")
    pio("device", "monitor", "-p", port, "-b", MONITOR_BAUD)


def cmd_erase(args):
    port = resolve_port(args.port)
    if not args.yes and not ask_yes_no(
            "This wipes the firmware and all settings saved on the device. Continue?"):
        info("Cancelled")
        return
    pio("run", "-e", PIO_ENV, "-t", "erase", "--upload-port", port)
    info("Erased. Run 'cyd flash' to install the firmware again.")


def cmd_ports(args):
    ports = list_ports()
    if not ports:
        print("No serial ports found.")
        return
    for port, desc, chip in ports:
        tag = f"  <- likely ESP32 board ({chip})" if chip else ""
        print(f"{port:<20} {desc}{tag}")


def firmware_name():
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(ROOT / "platformio.ini", encoding="utf-8")
    return ini.get(f"env:{PIO_ENV}", "custom_firmware_name")


def git_version():
    try:
        out = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"],
                             cwd=ROOT, capture_output=True, text=True, check=True)
        return out.stdout.strip() or "dev"
    except (OSError, subprocess.CalledProcessError):
        return "dev"


def cmd_release(args):
    version = args.version or git_version()
    name = firmware_name()
    info(f"Building {name} {version} (config.json is never included in release builds)")
    # The firmware reports this version to the flasher, so it must match the manifest
    pio("run", "-e", RELEASE_ENV, "-t", "mergebin", env={**os.environ, "CYD_FIRMWARE_VERSION": version})

    if DIST.exists():
        shutil.rmtree(DIST)
    shutil.copytree(WEB, DIST)

    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    bin_name = f"{slug}-{re.sub(r'[^A-Za-z0-9._-]+', '-', version)}.bin"
    shutil.copyfile(ROOT / ".pio" / "build" / RELEASE_ENV / "firmware-merged.bin", DIST / bin_name)

    # ESP Web Tools manifest: https://esphome.github.io/esp-web-tools/
    manifest = {
        "name": name,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32", "parts": [{"path": bin_name, "offset": 0}]}],
    }
    (DIST / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    info(f"Release files in {DIST.name}/: {bin_name}, manifest.json, flasher web page")

    if args.serve:
        serve(args.serve_port)


def serve(port):
    # Browsers only allow Web Serial on https:// pages or http://localhost
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(DIST))
    with http.server.ThreadingHTTPServer(("localhost", port), handler) as httpd:
        info(f"Flasher running at http://localhost:{port}/ - open it in Chrome or Edge. "
             "Press Ctrl+C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            info("Stopped")


def cmd_clean(args):
    pio("run", "-e", PIO_ENV, "-t", "clean")
    info("Build files removed")


# ---------------------------------------------------------------------------

EXAMPLES = """\
examples:
  cyd config               create config.json by answering a few questions
  cyd flash                build and upload to the board it finds
  cyd flash --monitor      ...then show the device's log output
  cyd flash --port COM5    upload to a specific port (see 'cyd ports')

Run 'cyd <command> --help' for a command's options.
"""


def build_parser():
    parser = argparse.ArgumentParser(
        prog="cyd",
        description="Build and flash the WLED remote firmware for the ESP32-2432S028 (CYD).",
        epilog=EXAMPLES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", metavar="<command>")

    def add(name, func, help_text):
        p = sub.add_parser(name, help=help_text, description=help_text)
        p.set_defaults(func=func)
        return p

    def add_port(p):
        p.add_argument("-p", "--port", help="serial port, e.g. COM5 or /dev/ttyUSB0 (default: auto-detect)")

    p = add("setup", cmd_setup, "install PlatformIO and the ESP32 toolchain (done automatically on first use)")
    p.add_argument("--force", action="store_true", help="delete and recreate the .venv environment")

    p = add("config", cmd_config, "create config.json with your WiFi and lights (optional)")
    p.add_argument("--copy", action="store_true",
                   help="just copy config.example.json to config.json for editing by hand")
    p.add_argument("--force", action="store_true", help="overwrite an existing config.json")

    p = add("build", cmd_build, "compile the firmware")
    p.add_argument("--clean", action="store_true", help="rebuild everything from scratch")

    p = add("flash", cmd_flash, "build and upload the firmware to the board")
    add_port(p)
    p.add_argument("-m", "--monitor", action="store_true", help="open the serial monitor afterwards")

    p = add("monitor", cmd_monitor, "show the device's serial log output")
    add_port(p)

    p = add("erase", cmd_erase, "wipe the board's flash, including all saved settings")
    add_port(p)
    p.add_argument("-y", "--yes", action="store_true", help="don't ask for confirmation")

    p = add("release", cmd_release,
            "build a distributable image and the browser flasher page into dist/")
    p.add_argument("--version", help="version to embed and publish (default: from 'git describe')")
    p.add_argument("--serve", action="store_true",
                   help="then serve the flasher page on localhost to try it out")
    p.add_argument("--serve-port", type=int, default=8000, metavar="PORT",
                   help="port for --serve (default: 8000)")

    add("ports", cmd_ports, "list serial ports and highlight likely boards")
    add("clean", cmd_clean, "delete build output")
    return parser


def main():
    if sys.version_info < (3, 8):
        sys.exit("Python 3.8 or newer is required.")

    parser = build_parser()
    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        return 1

    try:
        args.func(args)
    except CommandError as e:
        print(f"\nerror: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
