#/usr/bin/env bash
set -eu

cmake --toolchain toolchains/gnu/host.cmake -G Ninja -B build code

cmake --build build --target task

cp build/task .
