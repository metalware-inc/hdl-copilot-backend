@echo off
SETLOCAL ENABLEEXTENSIONS
SETLOCAL ENABLEDELAYEDEXPANSION

cd build
cmake ..
cmake --build . --config Release -- /maxcpucount:16
cd ..
