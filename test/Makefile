# Host-Tests: make test (g++ oder clang++). Unter Windows ohne g++: run_tests.cmd (zig-clang aus dem .venv)
CXX ?= g++
test: test_main.cpp ../components/ofen_logic/*.h
	$(CXX) -std=c++17 -Wall -Wextra -O1 -o test_main test_main.cpp
	./test_main
.PHONY: test
