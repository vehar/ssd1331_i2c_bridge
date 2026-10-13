#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ilib/Bridge lib/Bridge/Bridge.cpp test/test_bridge.cpp -o test/bridge_tests
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./test/bridge_tests
