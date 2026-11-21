SDK_DIR ?= ../sdk

CXX := sh4-elf-g++
LD := sh4-elf-ld

CXXFLAGS := -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar \
            -Wall -Wextra -O2 -m4a-nofpu \
            -Iinclude -I$(SDK_DIR)/include
DEPFLAGS := -MMD -MP

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
DEPFILES := $(OBJECTS:.o=.d)

all: ui.o

ui.o: $(OBJECTS)
	$(LD) -nostdlib -r -o $@ $(OBJECTS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(DEPFLAGS)

clean:
	rm -f $(OBJECTS) $(DEPFILES) ui.o

-include $(DEPFILES)

.PHONY: all clean
