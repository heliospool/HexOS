#!/bin/bash
# flash_device.sh — Flash a HexOS firmware + www to a device via esptool.
#
# Usage:
#   ./flash_device.sh                              # auto-detect port, latest release in .local/releases/
#   ./flash_device.sh -p /dev/ttyUSB0             # explicit port
#   ./flash_device.sh -f path/to/firmware.bin     # explicit firmware bin
#   ./flash_device.sh -w path/to/www.bin          # explicit www bin
#   ./flash_device.sh -r v2.13.1-hexos            # use specific release dir
#
# The script expects merged binaries (firmware.bin + www.bin) as produced by
# build_release.sh. These are separate OTA-compatible binaries, not a factory image.
#
# Flash addresses:
#   0x10000  esp-miner.bin   (firmware)
#   0x410000 www.bin         (web interface)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASES_DIR="$SCRIPT_DIR/releases"

PORT=""
FIRMWARE=""
WWW=""
RELEASE_TAG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p) PORT="$2";        shift 2 ;;
        -f) FIRMWARE="$2";    shift 2 ;;
        -w) WWW="$2";         shift 2 ;;
        -r) RELEASE_TAG="$2"; shift 2 ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-p port] [-f firmware.bin] [-w www.bin] [-r tag]"
            exit 1
            ;;
    esac
done

# Resolve firmware/www from release dir if not given explicitly
if [[ -z "$FIRMWARE" || -z "$WWW" ]]; then
    if [[ -n "$RELEASE_TAG" ]]; then
        RELEASE_DIR="$RELEASES_DIR/$RELEASE_TAG"
    else
        # Latest release dir by modification time
        RELEASE_DIR=$(ls -td "$RELEASES_DIR"/v*-hexos* 2>/dev/null | head -1)
        if [[ -z "$RELEASE_DIR" ]]; then
            echo "ERROR: No releases found in $RELEASES_DIR. Run build_release.sh first or specify -f/-w."
            exit 1
        fi
    fi

    [[ -z "$FIRMWARE" ]] && FIRMWARE=$(ls "$RELEASE_DIR"/HexOS-*-firmware.bin 2>/dev/null | head -1)
    [[ -z "$WWW"      ]] && WWW=$(ls "$RELEASE_DIR"/HexOS-*-www.bin 2>/dev/null | head -1)
fi

if [[ ! -f "$FIRMWARE" ]]; then
    echo "ERROR: Firmware binary not found: $FIRMWARE"
    exit 1
fi
if [[ ! -f "$WWW" ]]; then
    echo "ERROR: WWW binary not found: $WWW"
    exit 1
fi

# Auto-detect port if not given
if [[ -z "$PORT" ]]; then
    for candidate in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
        if [[ -e "$candidate" ]]; then
            PORT="$candidate"; break
        fi
    done
    if [[ -z "$PORT" ]]; then
        echo "ERROR: No device found. Use -p to specify port."
        exit 1
    fi
    echo "==> Port: $PORT (auto-detected)"
else
    echo "==> Port: $PORT"
fi

echo "==> Firmware: $(basename "$FIRMWARE")"
echo "==> WWW:      $(basename "$WWW")"
echo ""

esptool.py --chip esp32s3 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x10000  "$FIRMWARE" \
    0x410000 "$WWW"
