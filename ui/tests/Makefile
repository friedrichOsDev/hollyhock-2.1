SDK_DIR ?= ../../sdk
UI_DIR ?= ..
APP_TEMPLATE_DIR ?= ../../app_template
CXX := sh4-elf-g++
AS := sh4-elf-as
LD := sh4-elf-ld
OBJCOPY := sh4-elf-objcopy
CXXFLAGS := -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar -Wall -Wextra -O2 -m4a-nofpu -I$(SDK_DIR)/include -I$(UI_DIR)/include
HOST_CXX ?= g++
HOST_FLAGS := -std=c++11 -Wall -Wextra -I$(UI_DIR)/include

host-test: file_filter_test
	./file_filter_test
file_filter_test: file_filter_test.cpp $(UI_DIR)/src/types.cpp
	$(HOST_CXX) $(HOST_FLAGS) $^ -o $@

calculator-test: $(SDK_DIR)/sdk.o $(UI_DIR)/ui.o start.o calculator_api_test.o
	$(LD) -nostdlib --no-undefined -T $(APP_TEMPLATE_DIR)/linker_bin.ld -o ui_api_test.bin start.o calculator_api_test.o $(UI_DIR)/ui.o $(SDK_DIR)/sdk.o
	$(LD) -nostdlib --no-undefined -T $(APP_TEMPLATE_DIR)/linker_hhk.ld -o ui_api_test.hhk start.o calculator_api_test.o $(UI_DIR)/ui.o $(SDK_DIR)/sdk.o
	$(OBJCOPY) --set-section-flags .hollyhock_name=contents,strings,readonly ui_api_test.hhk ui_api_test.hhk
	$(OBJCOPY) --set-section-flags .hollyhock_description=contents,strings,readonly ui_api_test.hhk ui_api_test.hhk
	$(OBJCOPY) --set-section-flags .hollyhock_author=contents,strings,readonly ui_api_test.hhk ui_api_test.hhk
	$(OBJCOPY) --set-section-flags .hollyhock_version=contents,strings,readonly ui_api_test.hhk ui_api_test.hhk

$(SDK_DIR)/sdk.o:
	$(MAKE) -C $(SDK_DIR)
$(UI_DIR)/ui.o:
	$(MAKE) -C $(UI_DIR) SDK_DIR=../sdk
start.o: start.s
	$(AS) $< -o $@
calculator_api_test.o: calculator_api_test.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)
clean:
	rm -f file_filter_test *.o ui_api_test.bin ui_api_test.hhk
.PHONY: host-test calculator-test clean
