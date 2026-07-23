#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fake_bin="$tmp/bin"
state="$tmp/state"
prefix="$tmp/prefix"
build="$tmp/build"
home="$tmp/home"
cli_bin="$tmp/usr-local-bin/wechat-antirecall"
mkdir -p "$fake_bin" "$state" "$build" "$home"
printf 'test library\n' > "$build/libwechat-antirecall.so"

for cmd in cmake ctest pkg-config ninja c++ portable; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$fake_bin/$cmd"
done
cat > "$fake_bin/pgrep" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
cat > "$build/antirecall-inspect" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$fake_bin"/* "$build/antirecall-inspect"

export PATH="$fake_bin:$PATH"
export WECHAT_ANTI_RECALL_TESTING=1
export WECHAT_ANTI_RECALL_USER="$(id -un)"
export WECHAT_ANTI_RECALL_HOME="$home"
export PREFIX="$prefix"
export STATE_DIR="$state"
export BUILD_DIR="$build"
export CLI_BIN="$cli_bin"
export NO_RESTART=1

cli="$repo/wechat-antirecall"
env_file="$state/portable.env"
other_lib=/opt/example/libother.so
printf 'KEEP=this-value\nLD_PRELOAD=%s\n' "$other_lib" > "$env_file"

"$cli" install probe
[[ -x "$cli_bin" && -x "$prefix/antirecall-inspect" ]]
grep -Fx 'KEEP=this-value' "$env_file"
grep -Fq "$other_lib" "$env_file"
grep -Fq "$prefix/libwechat-antirecall.so" "$env_file"
grep -Fx 'WECHAT_ANTI_RECALL_PROBE_ONLY=1' "$env_file"

status=$("$cli" status)
grep -Fq 'Mode: probe' <<<"$status"
grep -Fq 'Runtime: WeChat is not running' <<<"$status"

printf 'AFTER_INSTALL=preserve-me\n' >> "$env_file"
"$cli" enable
grep -Fx 'AFTER_INSTALL=preserve-me' "$env_file"
grep -Fq "$other_lib" "$env_file"
if grep -q '^WECHAT_ANTI_RECALL_PROBE_ONLY=' "$env_file"; then
    echo 'enable left probe mode configured' >&2
    exit 1
fi

"$cli" disable
grep -Fx 'AFTER_INSTALL=preserve-me' "$env_file"
grep -Fq "$other_lib" "$env_file"
if grep -Fq "$prefix/libwechat-antirecall.so" "$env_file"; then
    echo 'disable left project preload configured' >&2
    exit 1
fi
[[ -f "$prefix/libwechat-antirecall.so" ]]

"$cli" uninstall
grep -Fx 'KEEP=this-value' "$env_file"
grep -Fx 'AFTER_INSTALL=preserve-me' "$env_file"
grep -Fq "$other_lib" "$env_file"
[[ ! -e "$prefix" && ! -e "$cli_bin" ]]

symlink_state="$tmp/symlink-state"
ln -s "$tmp" "$symlink_state"
mkdir -p "$prefix"
printf 'test library\n' > "$prefix/libwechat-antirecall.so"
if STATE_DIR="$symlink_state" "$cli" enable 2>/dev/null; then
    echo 'accepted symlinked state directory' >&2
    exit 1
fi

managed_state="$tmp/managed-only-state"
mkdir -p "$managed_state"
printf 'LD_PRELOAD=%s\nWECHAT_ANTI_RECALL_PROBE_ONLY=1\n' \
    "$prefix/libwechat-antirecall.so" > "$managed_state/portable.env"
STATE_DIR="$managed_state" "$cli" uninstall
[[ ! -e "$managed_state/portable.env" ]]

echo 'CLI_TEST_OK'
