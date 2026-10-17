CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra \
            -I.. \
            -I../../vendor/catch2/single_include \
            -I../../vendor/json/single_include

TARGET := run_tests

SRCS := test_tool_registry.cpp \
        ../tools/tool_registry.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): build/test_tool_registry.o build/tool_registry.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_tool_registry.o: test_tool_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/tool_registry.o: ../tools/tool_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET)

run: all
	./$(TARGET)
