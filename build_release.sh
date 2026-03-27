#!/bin/bash
# build_release.sh — Build and optionally publish a HexOS release.
#
# Builds from the current HEAD of the hexos branch (or a specified tag).
# Produces firmware.bin, www.bin, and per-board factory images.
#
# Usage:
#   ./build_release.sh                    # build only, tag from git describe
#   ./build_release.sh --tag v2.13.1-hexos
#   ./build_release.sh --publish          # build + publish to heliospool/HexOS
#   ./build_release.sh --tag v2.13.1-hexos --publish
#
# Requires: gh CLI authenticated, esptool.py, ESP-IDF at ~/esp/idf.
# Output: .local/releases/<tag>/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# Support the workspace layout where the git repo lives in a sibling 'github/' dir
if [[ -d "$REPO_ROOT/github/.git" ]]; then
    REPO_ROOT="$REPO_ROOT/github"
fi
IDF_PATH="${IDF_PATH:-$HOME/esp/idf}"
RELEASES_ROOT="$SCRIPT_DIR/releases"
PUBLISH=0
TAG=""

for arg in "$@"; do
    case "$arg" in
        --publish) PUBLISH=1 ;;
        --tag)     shift; TAG="$1" ;;
        --tag=*)   TAG="${arg#--tag=}" ;;
    esac
done

# Derive tag from git if not given
if [[ -z "$TAG" ]]; then
    TAG=$(git -C "$REPO_ROOT" describe --tags --exact-match 2>/dev/null || true)
    if [[ -z "$TAG" ]]; then
        echo "ERROR: HEAD is not on an exact tag. Use --tag v2.13.1-hexos or tag the commit first."
        exit 1
    fi
fi

echo "==> Tag:  $TAG"
echo "==> Repo: $REPO_ROOT"

# Source IDF
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ERROR: ESP-IDF not found at $IDF_PATH"
    exit 1
fi
# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" > /dev/null 2>&1

RELEASE_DIR="$RELEASES_ROOT/$TAG"
mkdir -p "$RELEASE_DIR"

# Build
echo "==> Running idf.py build"
idf.py -C "$REPO_ROOT" build
BUILD_DIR="$REPO_ROOT/build"

for f in "$BUILD_DIR/bootloader/bootloader.bin" \
          "$BUILD_DIR/partition_table/partition-table.bin" \
          "$BUILD_DIR/esp-miner.bin" \
          "$BUILD_DIR/www.bin" \
          "$BUILD_DIR/ota_data_initial.bin"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: Missing artifact: $f"
        exit 1
    fi
done

BOOTLOADER_ADDR=0x0
PARTITION_ADDR=0x8000
CONFIG_ADDR=0x9000
MINER_ADDR=0x10000
WWW_ADDR=0x410000
OTA_ADDR=0xf10000

NVS_GEN="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
ESPTOOL_BASE="esptool.py --chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 80m"

# 1. OTA firmware binary
FIRMWARE_OUT="$RELEASE_DIR/HexOS-${TAG}-firmware.bin"
echo "==> firmware:  $(basename "$FIRMWARE_OUT")"
cp "$BUILD_DIR/esp-miner.bin" "$FIRMWARE_OUT"

# 2. OTA www binary
WWW_OUT="$RELEASE_DIR/HexOS-${TAG}-www.bin"
echo "==> www:       $(basename "$WWW_OUT")"
cp "$BUILD_DIR/www.bin" "$WWW_OUT"

# 3. Per-board factory images (one per config-*.cvs in repo root)
FACTORY_OUTS=()
for CVS in "$REPO_ROOT"/config-*.cvs; do
    BOARD=$(basename "$CVS" .cvs | sed 's/^config-//')
    CONFIG_BIN="$RELEASE_DIR/config-${BOARD}.bin"
    FACTORY_OUT="$RELEASE_DIR/HexOS-${TAG}-factory-${BOARD}.bin"

    python3 "$NVS_GEN" generate "$CVS" "$CONFIG_BIN" 0x6000

    $ESPTOOL_BASE \
        $BOOTLOADER_ADDR "$BUILD_DIR/bootloader/bootloader.bin" \
        $PARTITION_ADDR  "$BUILD_DIR/partition_table/partition-table.bin" \
        $CONFIG_ADDR     "$CONFIG_BIN" \
        $MINER_ADDR      "$BUILD_DIR/esp-miner.bin" \
        $WWW_ADDR        "$BUILD_DIR/www.bin" \
        $OTA_ADDR        "$BUILD_DIR/ota_data_initial.bin" \
        -o "$FACTORY_OUT"

    rm -f "$CONFIG_BIN"
    cp "$CVS" "$RELEASE_DIR/$(basename "$CVS")"
    FACTORY_OUTS+=("$FACTORY_OUT")
    echo "==> factory:   $(basename "$FACTORY_OUT")"
done

echo ""
echo "==> Artifacts in $RELEASE_DIR:"
ls -lh "$RELEASE_DIR"

# Publish
if [[ "$PUBLISH" -eq 1 ]]; then
    # Immutability guard
    if gh release view "$TAG" --repo heliospool/HexOS > /dev/null 2>&1; then
        echo "==> Skipping publish — release $TAG already exists on GitHub (immutable)."
        exit 0
    fi

    # Detect upstream base tag from the hexos branch (strip -hexos suffix)
    UPSTREAM_BASE="${TAG%-hexos*}"
    UPSTREAM_PRERELEASE=$(gh api "repos/bitaxeorg/ESP-Miner/releases/tags/$UPSTREAM_BASE" \
        --jq '.prerelease' 2>/dev/null || echo "false")
    PRERELEASE_FLAG=""
    if [[ "$UPSTREAM_PRERELEASE" == "true" ]]; then
        PRERELEASE_FLAG="--prerelease"
        echo "==> Upstream $UPSTREAM_BASE is pre-release; marking HexOS release as pre-release."
    fi

    NOTES="## HexOS $TAG

Based on upstream ESP-Miner $UPSTREAM_BASE.

## Files

| File | Use |
|------|-----|
| \`HexOS-${TAG}-firmware.bin\` | AxeOS UI → OTA → Firmware |
| \`HexOS-${TAG}-www.bin\` | AxeOS UI → OTA → Web |
| \`HexOS-${TAG}-factory-<board>.bin\` | Clean flash (board-specific, includes config) |
| \`config-<board>.cvs\` | Config only via bitaxetool |

## Flashing

**AxeOS UI (OTA update):**
1. Settings → OTA Firmware → upload \`HexOS-${TAG}-firmware.bin\`
2. Settings → OTA Web → upload \`HexOS-${TAG}-www.bin\`

**bitaxetool (USB, clean flash with config):**
\`\`\`bash
pip install bitaxetool==0.6.1
bitaxetool --config config-<board>.cvs --firmware HexOS-${TAG}-factory-<board>.bin
\`\`\`

**esptool (USB, clean flash):**
\`\`\`bash
esptool.py --chip esp32s3 -b 460800 write_flash 0x0 HexOS-${TAG}-factory-<board>.bin
\`\`\`"

    ASSETS=("$FIRMWARE_OUT" "$WWW_OUT" "${FACTORY_OUTS[@]}")
    for CVS in "$RELEASE_DIR"/config-*.cvs; do
        ASSETS+=("$CVS")
    done

    echo "==> Publishing release $TAG to heliospool/HexOS..."
    gh release create "$TAG" \
        "${ASSETS[@]}" \
        --repo heliospool/HexOS \
        --title "HexOS $TAG" \
        --notes "$NOTES" \
        $PRERELEASE_FLAG

    echo "==> Published."
fi
