#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/protocol_reference.py
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I tests/firmware_fakes \
    tests/FirmwareTest.cpp firmware/SmartNoticeBoard/NoticeStore.cpp -o "$build_dir/firmware_test"
"$build_dir/firmware_test"
python3 wokwi/build_sketch.py > "$build_dir/sketch.ino"
cmp wokwi/sketch.ino "$build_dir/sketch.ino"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -DTEST_WOKWI_SKETCH \
    -I tests/firmware_fakes -I "$build_dir" tests/FirmwareTest.cpp -o "$build_dir/wokwi_test"
"$build_dir/wokwi_test"
echo "PASS: generated Wokwi sketch serial and LCD demo"
javac -d "$build_dir" android-app/app/src/main/java/com/example/electronicnoticeboard/BoardProtocol.java tests/BoardProtocolTest.java
java -cp "$build_dir" com.example.electronicnoticeboard.BoardProtocolTest
