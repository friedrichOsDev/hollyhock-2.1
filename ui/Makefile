SDK_DIR ?= ../sdk
CXX := sh4-elf-g++
CXXFLAGS := -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar -Wall -Wextra -O2 -m4a-nofpu -Iinclude -I$(SDK_DIR)/include
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
all: ui.o
ui.o: $(OBJECTS)
	sh4-elf-ld -nostdlib -r -o $@ $(OBJECTS)
%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)
clean:
	rm -f $(OBJECTS) ui.o
.PHONY: all clean
