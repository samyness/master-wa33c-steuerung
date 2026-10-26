@echo off
rem Host-Tests unter Windows ohne g++: nutzt den clang aus "pip install ziglang" im .venv
cd /d "%~dp0"
if not exist build mkdir build
..\.venv\Scripts\python -m ziglang c++ -std=c++17 -Wall -Wextra -O1 -o build\test_main.exe test_main.cpp || exit /b 1
build\test_main.exe
