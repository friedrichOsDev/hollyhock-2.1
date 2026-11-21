SDK_DIR ?= ../../sdk
UI_DIR ?= ..

CXX := g++
HOST_FLAGS := -std=c++11 -Wall -Wextra -Werror \
              -I$(UI_DIR)/include -I$(SDK_DIR)/include

host-test: ui_api_test
	./ui_api_test

ui_api_test: ui_api_test.cpp $(UI_DIR)/src/types.cpp $(UI_DIR)/src/filesystem.cpp
	$(CXX) $(HOST_FLAGS) $^ -o $@

clean:
	rm -f ui_api_test *.o

.PHONY: host-test clean
