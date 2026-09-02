#!/usr/bin/env bash
#
# Check the hand-written register map against ST's published SVD description.
#
# The SVD is generated from the same internal database as ST's own headers and
# the reference manual, so it is an independent statement of every base
# address, register offset and bit field on the part. Nothing is copied from
# it - the numbers are only compared - and any disagreement is reported for a
# human to resolve.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cache="${BSP_SVD_CACHE:-$here/.cache}"
svd="$cache/STM32F411.svd"
url="https://raw.githubusercontent.com/modm-io/cmsis-svd-stm32/main/stm32f4/STM32F411.svd"

mkdir -p "$cache"
if [ ! -s "$svd" ]; then
  echo "fetching $url"
  curl -fsSL -o "$svd" "$url"
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

python3 "$here/extract_bsp.py" > "$tmp"
python3 "$here/compare_svd.py" "$tmp" "$svd" "$@"
