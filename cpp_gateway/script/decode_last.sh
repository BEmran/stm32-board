mkdir -p csv_out

#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./decode_latest.sh [directory]
# If no directory is provided, it searches in the script location.

# Resolve script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Arguments
DIR="${1:-$SCRIPT_DIR}"
PREFIX="gateway_"

# Filename pattern: gateway_YYYYMMDD_HHMMSS_N.bin
pattern="^${PREFIX}[0-9]{8}_[0-9]{6}_[0-9]+\.bin$"

latest="$(
  find "$DIR" -maxdepth 1 -type f -printf '%f\n' \
  | grep -E "$pattern" \
  | sort -V \
  | tail -n 1
)"

if [[ -z "${latest:-}" ]]; then
  echo "No matching files found in '$DIR' with prefix '$PREFIX'" >&2
  exit 1
fi

bash "$SCRIPT_DIR/decode.sh" "$DIR/$latest"