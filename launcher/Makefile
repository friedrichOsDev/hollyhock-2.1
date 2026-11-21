ifneq ($(strip $(MAKECMDGOALS)),clean)
ifndef SDK_DIR
$(error You must define SDK_DIR and point it to the sdk/ folder)
endif
endif
UI_DIR ?= ../ui

AS := sh4-elf-as
CC := sh4-elf-gcc
CXX := sh4-elf-g++
LD := sh4-elf-ld
READELF := sh4-elf-readelf

AS_FLAGS :=
CC_FLAGS := -ffreestanding -fshort-wchar -Wall -Wextra -O2 -I$(SDK_DIR)/include/
DEPFLAGS := -MMD -MP
CXX_FLAGS := -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar \
             -Wall -Wextra -O2 -m4a-nofpu \
             -I$(SDK_DIR)/include/ -I$(UI_DIR)/include
LD_FLAGS := -nostdlib --no-undefined

AS_SOURCES := $(wildcard *.s)
CC_SOURCES := $(wildcard *.c)
CXX_SOURCES := $(wildcard *.cpp)
OBJECTS := $(AS_SOURCES:.s=.o) $(CC_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o)
DEPFILES := $(CC_SOURCES:.c=.d) $(CXX_SOURCES:.cpp=.d)
LIBS := $(SDK_DIR)/sdk.o $(UI_DIR)/ui-launcher.o

all: run.bin Makefile
clean:
	rm -f $(OBJECTS) $(DEPFILES) run.bin

run.bin: $(OBJECTS) $(LIBS) linker.ld
	$(LD) -T linker.ld -o $@ $(LD_FLAGS) $(OBJECTS) $(LIBS)

# SDK remains an explicit prerequisite, matching the previous build workflow.
$(SDK_DIR)/sdk.o:
	$(error SDK object not found. Build it first with: make -C $(SDK_DIR))

# Keep the UI library synchronized for direct make -C launcher builds.
.PHONY: FORCE_UI
FORCE_UI:
$(UI_DIR)/ui-launcher.o: FORCE_UI
	$(MAKE) -C $(UI_DIR) SDK_DIR=$(abspath $(SDK_DIR)) ui-launcher.o

%.o: %.s
	$(AS) $< -o $@ $(AS_FLAGS)
%.o: %.c
	$(CC) -c $< -o $@ $(CC_FLAGS) $(DEPFLAGS)
%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXX_FLAGS) $(DEPFLAGS)
	@$(READELF) $@ -S | grep -F ".ctors" > /dev/null && echo "ERROR: Global constructors aren't supported." && rm $@ && exit 1 || exit 0

main.o screen.o: screen.hpp
-include $(DEPFILES)

.PHONY: all clean
