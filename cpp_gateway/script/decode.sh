#!/usr/bin/env bash
set -euo pipefail

# ==============================
# Configuration
# ==============================

EXECUTABLE=./build/decode_gateway_log   # Path to compiled C++ binary
OUT_DIR="./csv_out"                    # Path to output directory
# ==============================
# Check Input Argument
# ==============================

if [[ $# -lt 1 ]]; then
    echo "ERROR: No input file provided."
    echo "Usage: $0 <input_file>"
    exit 1
fi

INPUT_FILE="$1"

# ==============================
# Validate Executable
# ==============================

if [[ ! -x "$EXECUTABLE" ]]; then
    echo "ERROR: Executable '$EXECUTABLE' not found or not executable."
    exit 1
fi

# ==============================
# Validate Input File
# ==============================

if [[ ! -f "$INPUT_FILE" ]]; then
    echo "ERROR: Input file '$INPUT_FILE' does not exist."
    exit 1
fi

# ==============================
# Run Program
# ==============================

echo "Running: $EXECUTABLE $INPUT_FILE and storing result at $OUT_DIR"
echo "-----------------------------------"

mkdir -p "$OUT_DIR"
"$EXECUTABLE" --in "$INPUT_FILE" --out_dir "$OUT_DIR"
