#!/usr/bin/env bash
set -euo pipefail

prefix=${PREFIX:-/usr/lib/wechat-antirecall}
state_dir=${STATE_DIR:-/home/cola/.local/share/WeChat_Data}
env_file="$state_dir/portable.env"
backup="$env_file.wechat-antirecall.bak"

if [[ ${EUID} -ne 0 ]]; then
  echo "uninstall.sh must run as root" >&2
  exit 1
fi

if [[ -f "$backup" ]]; then
  mv -f "$backup" "$env_file"
  chown cola:cola "$env_file"
  chmod 0600 "$env_file"
elif [[ -f "$env_file" ]]; then
  tmp=$(mktemp)
  grep -vE '^(LD_PRELOAD|WECHAT_ANTI_RECALL_PROBE_ONLY|WECHAT_ANTI_RECALL_VERBOSE)=' "$env_file" > "$tmp" || true
  install -o cola -g cola -m 0600 "$tmp" "$env_file"
  rm -f "$tmp"
fi
rm -rf "$prefix"
echo "Uninstalled. Fully quit WeChat and launch it again."
