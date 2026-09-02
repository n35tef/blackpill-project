#!/usr/bin/env bash
#
# Run every dry check there is: the register map against ST's SVD, then the
# drivers themselves against a simulated peripheral space.
#
# Neither needs a board. Neither replaces one. See tools/README.md for what
# each one does and does not prove.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0

echo "=============================================================="
echo " 1/2  register map vs ST SVD"
echo "=============================================================="
"$here/svdcheck/run.sh" || status=1

echo
echo "=============================================================="
echo " 2/2  drivers vs simulated peripherals"
echo "=============================================================="
"$here/hostsim/run.sh" || status=1

echo
if [ "$status" -eq 0 ]; then
  echo "all dry checks passed"
else
  echo "DRY CHECKS FAILED"
fi
exit "$status"
