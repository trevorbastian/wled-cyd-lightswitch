# PlatformIO post script: adds a "mergebin" target that combines the bootloader,
# partition table, boot_app0 and app into one image to be flashed at offset 0,
# which is what the browser flasher (ESP Web Tools) needs.
#
#   pio run -e release -t mergebin  ->  $BUILD_DIR/firmware-merged.bin

Import("env")  # noqa: F821 (provided by PlatformIO)

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
MERGED_BIN = "$BUILD_DIR/${PROGNAME}-merged.bin"


def merge_bin(source, target, env):
    board = env.BoardConfig()
    # FLASH_EXTRA_IMAGES holds (offset, path) pairs for everything except the app
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, path]
    images += ["$ESP32_APP_OFFSET", APP_BIN]

    cmd = [
        "$PYTHONEXE", "$OBJCOPY",  # $OBJCOPY is esptool.py on ESP32
        "--chip", board.get("build.mcu", "esp32"),
        "merge_bin",
        "--flash_mode", board.get("build.flash_mode", "dio"),
        "--flash_size", board.get("upload.flash_size", "4MB"),
        "-o", MERGED_BIN,
        *images,
    ]
    if env.Execute(" ".join(f'"{c}"' for c in cmd)):
        env.Exit(1)


env.AddCustomTarget(  # noqa: F821
    name="mergebin",
    dependencies=APP_BIN,
    actions=merge_bin,
    title="Merge binary",
    description="Combine bootloader, partitions and app into one image for web flashing",
)
