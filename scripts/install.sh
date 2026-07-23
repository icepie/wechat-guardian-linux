#!/usr/bin/env bash
set -euo pipefail

prefix=${PREFIX:-/usr/lib/wechat-antirecall}
state_dir=${STATE_DIR:-/home/cola/.local/share/WeChat_Data}
build_dir=${BUILD_DIR:-build}
mode=${1:-blocking}

case "$mode" in
  blocking|probe) ;;
  *) echo "usage: $0 [blocking|probe]" >&2; exit 2 ;;
esac

if [[ ${EUID} -ne 0 ]]; then
  echo "install.sh must run as root" >&2
  exit 1
fi

install -d -m 0755 "$prefix"
install -m 0755 "$build_dir/libwechat-antirecall.so" "$prefix/libwechat-antirecall.so"

install -d -o cola -g cola -m 0755 "$state_dir"
env_file="$state_dir/portable.env"
backup="$env_file.wechat-antirecall.bak"
if [[ -f "$env_file" && ! -f "$backup" ]]; then
  cp -a "$env_file" "$backup"
fi

tmp=$(mktemp)
if [[ -f "$env_file" ]]; then
  grep -vE '^(LD_PRELOAD|WECHAT_ANTI_RECALL_PROBE_ONLY|WECHAT_ANTI_RECALL_VERBOSE)=' "$env_file" > "$tmp" || true
fi
printf 'LD_PRELOAD=%s/libwechat-antirecall.so\n' "$prefix" >> "$tmp"
if [[ "$mode" == probe ]]; then
  printf 'WECHAT_ANTI_RECALL_PROBE_ONLY=1\n' >> "$tmp"
fi
install -o cola -g cola -m 0600 "$tmp" "$env_file"
rm -f "$tmp"

echo "Installed in $mode mode. Fully quit WeChat and launch it again."
 echo "Environment file: $env_file"
 echo "Library: $prefix/libwechat-antirecall.so"
