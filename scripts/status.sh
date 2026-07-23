#!/usr/bin/env bash
set -euo pipefail

env_file=${STATE_DIR:-/home/cola/.local/share/WeChat_Data}/portable.env
lib=${PREFIX:-/usr/lib/wechat-antirecall}/libwechat-antirecall.so
pid=$(pgrep -n -x wechat || true)

echo "Environment file: $env_file"
if [[ -f "$env_file" ]]; then
  grep -E '^(LD_PRELOAD|WECHAT_ANTI_RECALL_PROBE_ONLY|WECHAT_ANTI_RECALL_VERBOSE)=' "$env_file" || true
else
  echo "  not found"
fi
[[ -r "$lib" ]] && echo "Library: installed ($lib)" || echo "Library: not installed"
if [[ -n "$pid" ]]; then
  echo "WeChat PID: $pid"
  if grep -q 'libwechat-antirecall.so' "/proc/$pid/maps" 2>/dev/null; then
    echo "Runtime: loaded"
  else
    echo "Runtime: not loaded (restart WeChat after installation)"
  fi
else
  echo "WeChat: not running"
fi
