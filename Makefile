.DEFAULT_GOAL := renderer
.PHONY: clean compile_debug
MAKEFLAGS += --no-print-directory


MODE:=Release

BUILD=build
BUILD_REQ=$(BUILD)/Makefile

GENERATOR="Ninja Multi-Config"


renderer: # $(BUILD_REQ)
	@mkdir -p dist
	@cmake -S . -B build -G ${GENERATOR} -DCMAKE_INSTALL_PREFIX=dist/ -DCMAKE_BUILD_TYPE=$(MODE) -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@#ninja -C build
	@cmake --build $(BUILD) --config $(MODE)
	@cmake --install $(BUILD) --config $(MODE)
	@cp build/compile_commands.json .


clean:
	@rm -rf build



compile_debug:
	@mkdir -p build
	@cmake -S . -B build -G ${GENERATOR} -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ninja -C build
	@cmake --install build --config Debug
	@cp build/compile_commands.json .
