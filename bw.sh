#!/bin/bash
mkdir -p build_windows && \
x86_64-w64-mingw32-g++ *.cpp \
  $(/usr/x86_64-w64-mingw32/sys-root/mingw/bin/wx-config --cxxflags --libs std,core) \
  -o build_windows/search.exe \
  -static -static-libgcc -static-libstdc++ \
  -Wl,--subsystem,windows
