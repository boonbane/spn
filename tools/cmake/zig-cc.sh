#!/usr/bin/env sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$ROOT/.cache/zig}"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$ROOT/.cache/zig}"
ZIG="$(sh "$ROOT/tools/cmake/zig-toolchain.sh")"
ulimit -Sn "$(ulimit -Hn)" 2>/dev/null || ulimit -Sn 10240 2>/dev/null
exec "$ZIG" cc "$@"
