#!/bin/zsh
find . \( -path ./src/third-party -o -path ./build \) -prune -o \( -iname "*.h" -o -iname "*.cpp" \) -print | xargs clang-format -i
