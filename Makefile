all: install 

install: compile
	cmake --install build 

compile: build
	cmake --build build -j 16

build: 
	cmake -B build \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DCMAKE_INSTALL_PREFIX=install
	-DCMAKE_BUILD_TYPE=Debug  

format:
	find ./src -regex '.*\.[c|h]pp' -exec  clang-format -style=llvm -i {} +

clean:
	rm -rf install build

.PHONY: all clean build compile install
