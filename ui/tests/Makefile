SDK_DIR ?= ../../sdk
UI_DIR ?= ..
CXX := g++
HOST_FLAGS := -std=c++11 -Wall -Wextra -I$(UI_DIR)/include

host-test: file_filter_test
	./file_filter_test

file_filter_test: file_filter_test.cpp $(UI_DIR)/src/types.cpp
	$(CXX) $(HOST_FLAGS) $^ -o $@

clean:
	rm -f file_filter_test *.o

.PHONY: host-test clean
