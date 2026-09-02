#!/usr/bin/env bash
#
# Build and run the host simulation of the BSP drivers.
#
# The drivers are guarded by the BSP_USE_* switches in Bsp/Inc/bsp_config.h, so
# the simulation needs a build where everything is on. Rather than keeping a
# second copy of the configuration (which would drift), the real one is copied
# and its switches are flipped, then that directory is put first on the include
# path so it shadows the original. bsp_config.h therefore stays the single
# source of truth for what the options are.
#
# It has to be force-included rather than merely put on the include path: a
# quoted #include searches the including file's own directory first, so
# Bsp/Inc/bsp.h would always find the real Bsp/Inc/bsp_config.h. Force-including
# the copy first means its include guard is already defined by the time the
# original is reached, so the original is skipped.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/include"

sed -E \
  -e 's/^(#define (BSP_USE_[A-Z0-9_]+|BSP_STDOUT_USART)[ ]+)0/\11/' \
  -e 's/^(#define BSP_I2C2_FREQ_HZ[ ]+)100000UL/\1400000UL/' \
  "$repo/Bsp/Inc/bsp_config.h" > "$work/include/bsp_config.h"

# The simulator provides its own entry point and cannot use the newlib stubs or
# the reset handler, so those two translation units are left out.
sources=()
while IFS= read -r f; do sources+=("$f"); done < <(
  find "$repo/Bsp/Src" -name '*.c' \
    ! -name 'bsp_syscalls.c' \
    ! -name 'startup_stm32f411.c' | sort
)

# -Wno-overflow: the register bit macros are 1UL << n, which is 64-bit on the
# host and 32-bit on ARM, so writes like `SR = ~UIF` look like truncation here
# while being exact on the target. Everything else stays a hard warning.
cc=${CC:-gcc}
"$cc" -std=c11 -O1 -g \
  -DBSP_HOST_SIM \
  -Wall -Wextra -Wno-unused-parameter -Wno-overflow \
  -include "$work/include/bsp_config.h" \
  -I "$repo/Bsp/Inc" \
  "$here/sim.c" "${sources[@]}" \
  -o "$work/sim" -lpthread

"$work/sim"
