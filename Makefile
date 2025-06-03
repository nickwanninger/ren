.DEFAULT_GOAL := renderer
MAKEFLAGS += --no-print-directory


BUILD=build
BUILD_REQ=$(BUILD)/Makefile

$(BUILD)/Makefile:
	mkdir -p $(BUILD)
	cd $(BUILD) && cmake ../ -DCMAKE_INSTALL_PREFIX:PATH=$(ROOT)/local

renderer: $(BUILD_REQ)
	@cd $(BUILD) && cmake --build . --config Release
	@cp build/compile_commands.json .
