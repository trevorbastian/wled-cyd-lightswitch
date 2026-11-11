# WLED Remote for the ESP32 "Cheap Yellow Display"

Touchscreen remote for [WLED](https://kno.wled.ge/) lights, for the ESP32-2432S028R
("CYD") board. Turn up to 4 WLED controllers on and off and set their colour. The
screen updates instantly when the lights are changed from anywhere else.

WiFi and lights are set up on the touchscreen. WiFi can also be entered in the
browser right after installing. If you build it yourself, you can pre-fill both in
a `config.json`.

## Installing (no building needed)

Open **https://trevorbastian.github.io/wled-cyd-lightswitch/** in Chrome or Edge on
a computer, plug in the board, and click **Install**. Ready-made firmware files are
also on the [releases page](https://github.com/trevorbastian/wled-cyd-lightswitch/releases).

## Building and flashing

You only need **Python 3.8+**. The `cyd` script installs everything else (PlatformIO
and the ESP32 toolchain) into a local `.venv` folder the first time you run it.

| Windows | macOS / Linux |
|---|---|
| `cyd flash` | `./cyd flash` |

That builds the firmware and uploads it to the board it finds on USB. Other commands:

```
cyd config      create config.json with your WiFi and lights (optional)
cyd build       compile only
cyd monitor     show the device's log output
cyd ports       list serial ports
cyd erase       wipe the board, including saved settings
cyd release     build the release image and flasher page into dist/
cyd --help      all commands and options
```

### Troubleshooting

- **No board found:** use a USB data cable, not a charge-only one. On Windows and
  macOS, install the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html).
- **Permission denied (Linux):** `sudo usermod -aG dialout $USER`, then log out and back in.
- **Upload stalls at "Connecting...":** hold the board's **BOOT** button until it starts.
- **Inverted colours or blank screen:** you may have a two-USB-port variant. Change
  `-DILI9341_2_DRIVER=1` to `-DST7789_DRIVER=1` in `platformio.ini`.

## Default settings (`config.json`)

This file is optional. Create it with `cyd config` (it asks a few questions) or
`cyd config --copy` (copies [config.example.json](config.example.json) for you to
edit). Every section is optional:

```json
{
  "wifi":   { "ssid": "MyNetwork", "password": "secret" },
  "lights": [ { "name": "Living Room", "host": "192.168.1.50" } ],
  "touch":  { "x_min": 200, "x_max": 3700, "y_min": 240, "y_max": 3800 }
}
```

- The file is built into the firmware. Mistakes are reported when you build.
- Changes made on the touchscreen survive reflashing, unless you edit `config.json`.
  In that case the new file takes over.
- **Settings → Restore defaults** on the device goes back to these values.
- `config.json` is ignored by git because it contains your WiFi password.

## Releasing

Pushing a version tag builds and publishes a release automatically
([.github/workflows/release.yml](.github/workflows/release.yml)):

```
git tag v1.0.0
git push origin v1.0.0
```

This builds the firmware, attaches it to a GitHub Release, and updates the
browser flasher on GitHub Pages.

- **Pre-releases:** tags with a suffix, such as `v1.1.0-beta.1`, are marked as
  pre-releases and don't replace the version on the flasher page.
- **Your settings stay private:** release builds never include `config.json`.
- **Version number:** the version comes from the tag. It's shown in the device's
  Settings screen and reported to the flasher.

**One-time setup:** in the repository on GitHub, go to **Settings → Pages** and set
**Source** to **GitHub Actions**. On GitHub's free plan, Pages only works for public
repositories. While the repository is private, the Pages step is skipped, but the
build and Release still run.

**To try the flasher before releasing**, run `cyd release --serve` and open
http://localhost:8000 in Chrome or Edge.
