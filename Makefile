.DEFAULT_GOAL := renderer
.PHONY: clean
MAKEFLAGS += --no-print-directory


BUILD=build
BUILD_REQ=$(BUILD)/Makefile

$(BUILD)/Makefile:
	mkdir -p $(BUILD)
	cd $(BUILD) && cmake ../ -DCMAKE_INSTALL_PREFIX:PATH=$(ROOT)/local -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

renderer: $(BUILD_REQ)
	@cd $(BUILD) && cmake --build . --config Release
	@cp build/compile_commands.json .


clean:
	@rm -rf build
