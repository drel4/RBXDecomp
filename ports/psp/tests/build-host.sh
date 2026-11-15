#!/bin/sh
# Host checks compile the same adapted engine VM as PSP, including C++ exceptions.
set -eu
cd "$(dirname "$0")/.."
python3 tools/stage_lua.py
mkdir -p host-test
flags="-O1 -g -fsanitize=address,undefined -Wall -Wextra"
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
for unit in lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lmathlib ltablib lstrlib; do
    g++ $flags -std=c++17 -x c++ -Ivendor/lua-5.1.1/src -c "vendor/lua-5.1.1/src/$unit.c" -o "host-test/$unit.o"
done
# Compile one consistent core ABI, then link the independent behavioral checks.
mkdir -p host-test/core
for unit in scene runtime schema math3d collision physics player script script_values; do
    g++ $flags -std=c++17 -Ivendor/lua-5.1.1/src -c "$unit.cpp" -o "host-test/core/$unit.o"
done
for check in script_check bindings_check runtime_check scene_check physics_check physics_script_check player_check lifetime_check; do
    g++ $flags -std=c++17 -Ivendor/lua-5.1.1/src "tests/$check.cpp" host-test/core/*.o host-test/*.o -lm -o "$check"
done
./script_check tests/RuntimeFixture.rbxl
./bindings_check tests/RuntimeFixture.rbxl
./runtime_check tests/RuntimeFixture.rbxl
./physics_check
./physics_script_check
./player_check
./lifetime_check
python3 tests/test_loader.py
./scene_check places/RenderTest.rbxl
