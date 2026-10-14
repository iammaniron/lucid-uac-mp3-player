#!/usr/bin/env bash
# Source this helper for the legacy ESP-IDF beep example.
# Set IDF_PATH to an ESP-IDF 5.5.5 checkout first.
if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/export.sh" ]]; then
  printf '%s\n' 'Set IDF_PATH to your ESP-IDF 5.5.5 checkout, then source tools/idf-env.sh.' >&2
  return 1 2>/dev/null || exit 1
fi
source "$IDF_PATH/export.sh"
