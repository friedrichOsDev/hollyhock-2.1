CXX ?= c++
TARGET := cpv_convert
CXXFLAGS ?= -O3 -march=native -flto -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -D_FILE_OFFSET_BITS=64
LDFLAGS ?=
LDLIBS ?= -lz -llz4

.PHONY: all clean test
all: $(TARGET)

$(TARGET): convert.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

test: $(TARGET)
	./$(TARGET) --self-test
	python3 -m unittest -v test_encoder

clean:
	rm -f $(TARGET)
