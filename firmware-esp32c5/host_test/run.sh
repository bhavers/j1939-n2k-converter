#!/bin/sh
# Builds and runs the host unit tests. No board needed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for t in fuel_model j1939_dm1 bus_status; do
  c++ -std=c++17 -Wall -Wextra -Werror -I "$here/../src" \
      "$here/test_$t.cpp" -o "$out/test_$t"
  echo "== $t"
  "$out/test_$t"
done
