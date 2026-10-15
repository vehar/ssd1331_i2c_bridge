#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
outdir=$(mktemp -d)
trap 'rm -rf "$outdir"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ilib/Bridge -Iinclude)
for scale in 0 1; do
    g++ "${flags[@]}" -DBRIDGE_SCALE_TO_FIT=$scale lib/Bridge/Bridge.cpp test/test_bridge.cpp -o "$outdir/bridge"
    "$outdir/bridge"
done
g++ "${flags[@]}" lib/Bridge/Bridge.cpp src/cli/serial_cli.cpp src/cli/cmd_parser.cpp src/cli/app_cli_commands.cpp src/cli/app_info.cpp test/test_cli.cpp -o "$outdir/cli"
"$outdir/cli"
echo "PASS: CLI parsing, colors, text blits, rectangle, clear, invalid input and capture"
