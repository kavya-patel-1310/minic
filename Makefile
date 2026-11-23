CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc -MMD -MP

LIB_SRC  := $(filter-out src/driver/main.cpp,$(wildcard src/*/*.cpp))
LIB_OBJ  := $(LIB_SRC:src/%.cpp=build/obj/%.o)
MAIN_OBJ := build/obj/driver/main.o
TEST_OBJ := build/obj/tests/unit_tests.o

BIN      := minic
TEST_BIN := build/unit_tests

.PHONY: all test unit integration bench sanitize clean

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(LIB_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJ) $(LIB_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/obj/tests/%.o: tests/unit/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

test: unit integration

unit: $(TEST_BIN)
	./$(TEST_BIN)

integration: $(BIN)
	./tests/run_tests.sh

bench: $(BIN)
	./benchmarks/run.sh

# Build and test with AddressSanitizer + UndefinedBehaviorSanitizer in a separate tree.
sanitize:
	$(MAKE) clean
	$(MAKE) test CXXFLAGS="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra" \
	             LDFLAGS="-fsanitize=address,undefined"

clean:
	rm -rf build $(BIN)

-include $(LIB_OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
