#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/protocol_reference.py
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I tests/firmware_fakes \
    tests/FirmwareTest.cpp firmware/SmartNoticeBoard/NoticeStore.cpp -o "$build_dir/firmware_test"
"$build_dir/firmware_test"
javac -d "$build_dir" android-app/app/src/main/java/com/example/electronicnoticeboard/BoardProtocol.java tests/BoardProtocolTest.java
java -cp "$build_dir" com.example.electronicnoticeboard.BoardProtocolTest
