#!/bin/bash

# Defaults
HOST="${1:-127.0.0.1}"
PORT="${2:-20003}"

echo -n "status" | nc -u -w1 "$HOST" "$PORT"