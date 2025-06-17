.DEFAULT_GOAL := renderer
.PHONY: clean compile_debug
MAKEFLAGS += --no-print-directory


BUILD=build
BUILD_REQ=$(BUILD)/Makefile

$(BUILD)/Makefile:
	mkdir -p $(BUILD)
	cd $(BUILD) && cmake ../ -DCMAKE_INSTALL_PREFIX:PATH=$(ROOT)/local -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

renderer: $(BUILD_REQ)
	@cd $(BUILD) && cmake --build . --config Debug
	@cp build/compile_commands.json .


clean:
	@rm -rf build



compile_debug:
	@mkdir -p build
	@cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ninja -C build
	@cp build/compile_commands.json .