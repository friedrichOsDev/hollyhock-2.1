# Build app_template and link the SDK and custom UI library.
# Example: make SDK_DIR=../sdk UI_DIR=../ui all

APP_NAME := app_template

ifndef SDK_DIR
$(error You need to define SDK_DIR and point it to the sdk/ folder)
endif

UI_DIR ?= ../ui

AS := sh4-elf-as
AS_FLAGS :=

CC := sh4-elf-gcc
CC_FLAGS := -ffreestanding -fshort-wchar -Wall -Wextra -O2 -I$(SDK_DIR)/include/

CXX := sh4-elf-g++
CXX_FLAGS := -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar -Wall -Wextra -O2 -I$(SDK_DIR)/include/ -I$(UI_DIR)/include -m4a-nofpu

LD := sh4-elf-ld
LD_FLAGS := -nostdlib --no-undefined

READELF := sh4-elf-readelf
OBJCOPY := sh4-elf-objcopy

AS_SOURCES := $(wildcard *.s)
CC_SOURCES := $(wildcard *.c)
CXX_SOURCES := $(wildcard *.cpp demo/*.cpp)
OBJECTS := $(AS_SOURCES:.s=.o) $(CC_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o)

APP_ELF := $(APP_NAME).hhk
APP_BIN := $(APP_NAME).bin
LIBS := $(SDK_DIR)/sdk.o $(UI_DIR)/ui.o

bin: $(APP_BIN) Makefile
hhk: $(APP_ELF) Makefile
all: $(APP_ELF) $(APP_BIN) Makefile

clean:
	rm -f $(OBJECTS) $(APP_ELF) $(APP_BIN)

$(APP_ELF): $(OBJECTS) $(LIBS) linker_hhk.ld
	$(LD) -T linker_hhk.ld -o $@ $(LD_FLAGS) $(OBJECTS) $(LIBS)
	$(OBJCOPY) --set-section-flags .hollyhock_name=contents,strings,readonly $@ $@
	$(OBJCOPY) --set-section-flags .hollyhock_description=contents,strings,readonly $@ $@
	$(OBJCOPY) --set-section-flags .hollyhock_author=contents,strings,readonly $@ $@
	$(OBJCOPY) --set-section-flags .hollyhock_version=contents,strings,readonly $@ $@

$(APP_BIN): $(OBJECTS) $(LIBS) linker_bin.ld
	$(LD) --oformat binary -T linker_bin.ld -o $@ $(LD_FLAGS) $(OBJECTS) $(LIBS)

$(SDK_DIR)/sdk.o:
	$(error SDK object not found. Build the SDK first with: make -C $(SDK_DIR))

$(UI_DIR)/ui.o:
	$(MAKE) -C $(UI_DIR) SDK_DIR=$(SDK_DIR)

%.o: %.s
	$(AS) $< -o $@ $(AS_FLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CC_FLAGS)

# Global constructors are unsupported by the freestanding runtime.
%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXX_FLAGS)
	@$(READELF) $@ -S | grep ".ctors" > /dev/null && echo "ERROR: Global constructors aren't supported." && rm $@ && exit 1 || exit 0

# Rebuild both demo translation units when their shared declarations change.
main.o demo/demo.o: demo/demo.hpp

.PHONY: bin hhk all clean
