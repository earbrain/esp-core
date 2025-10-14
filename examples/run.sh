#!/bin/bash
set -e

EXAMPLES=(wifi_ap wifi_sta wifi_scan wifi_test mdns smartconfig logging metrics tasks)

if [ $# -eq 0 ]; then
  echo "Usage: $0 <example>"
  echo ""
  echo "Available examples:"
  for ex in "${EXAMPLES[@]}"; do
    echo "  $ex"
  done
  exit 1
fi

EXAMPLE=$1

if [[ ! " ${EXAMPLES[@]} " =~ " ${EXAMPLE} " ]]; then
  echo "Error: Unknown example '$EXAMPLE'"
  exit 1
fi

idf.py -DEXAMPLE=$EXAMPLE build flash monitor
