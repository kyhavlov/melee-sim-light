#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc tools/viewer/live/debug_gcadapter_libusb.c \
  $(pkg-config --cflags --libs libusb-1.0) \
  -o reports/triage/debug_gcadapter_libusb
reports/triage/debug_gcadapter_libusb
