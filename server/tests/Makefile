CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra \
            -I.. \
            -Iinclude \
            -I../http \
            -I../controllers \
            -I../services \
            -I../tools \
            -I../../vendor/catch2/single_include \
            -I../../vendor/json/single_include

.PHONY: all clean run

all: run_tool_registry_tests run_file_transfer_tests run_ping_tests run_mcp_controller_tests run_mcp_service_tests run_resource_registry_tests

# ── tool registry ────────────────────────────────────────────────────────────

run_tool_registry_tests: build/test_tool_registry.o build/tool_registry.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_tool_registry.o: tools/test_tool_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/tool_registry.o: ../tools/tool_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── file transfer controller ─────────────────────────────────────────────────

run_file_transfer_tests: build/test_file_transfer_controller.o \
                         build/file_transfer_controller.o \
                         build/mock_file_transfer_service.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_file_transfer_controller.o: controllers/test_file_transfer_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/file_transfer_controller.o: ../controllers/file_transfer_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/mock_file_transfer_service.o: services/mock_file_transfer_service.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── ping controller ──────────────────────────────────────────────────────────

run_ping_tests: build/test_ping_controller.o build/ping_controller.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_ping_controller.o: controllers/test_ping_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/ping_controller.o: ../controllers/ping_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── mcp controller ───────────────────────────────────────────────────────────

run_mcp_controller_tests: build/test_mcp_controller.o \
                           build/mcp_controller.o \
                           build/mock_mcp_service.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_mcp_controller.o: controllers/test_mcp_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/mcp_controller.o: ../controllers/mcp_controller.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/mock_mcp_service.o: services/mock_mcp_service.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── mcp service ──────────────────────────────────────────────────────────────

run_mcp_service_tests: build/test_mcp_service.o build/mcp_service.o build/tool_registry.o \
                       build/resource_registry.o build/stub_led_notif.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_mcp_service.o: services/test_mcp_service.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/mcp_service.o: ../services/mcp_service.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/stub_led_notif.o: services/stub_led_notif.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── resource registry ────────────────────────────────────────────────────────

run_resource_registry_tests: build/test_resource_registry.o build/resource_registry.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/test_resource_registry.o: services/test_resource_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/resource_registry.o: ../services/resource_registry.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ─────────────────────────────────────────────────────────────────────────────

clean:
	rm -rf build run_tool_registry_tests run_file_transfer_tests run_ping_tests run_mcp_controller_tests run_mcp_service_tests run_resource_registry_tests

run: all
	./run_tool_registry_tests
	./run_file_transfer_tests
	./run_ping_tests
	./run_mcp_controller_tests
	./run_mcp_service_tests
	./run_resource_registry_tests
