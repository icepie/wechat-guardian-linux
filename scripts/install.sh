#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec "$repo/wechat-guardian" install "${1:-blocking}"
