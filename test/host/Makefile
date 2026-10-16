CXX ?= g++
BUILD ?= build
STD := -std=gnu++11
WARN := -Wall -Wextra -Wno-unused-parameter
SRC := ../../src
SRCS := $(filter-out $(SRC)/main.cpp $(SRC)/platform.cpp,$(wildcard $(SRC)/*.cpp))
TESTS := $(wildcard tests/*.cpp)
TEST_INC := -Istubs -Ifixtures -I../../include -I$(SRC) -I.
ALL_SRCS := $(wildcard $(SRC)/*.cpp)

.PHONY: all test check clean

all: test check

$(BUILD)/runner: $(SRCS) $(TESTS) runner.cpp stubs/stubs.cpp testlib.h $(wildcard stubs/*.h) $(wildcard $(SRC)/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(STD) $(WARN) -g -DRELEASE_BUILD -DLOG_ENABLED=0 $(TEST_INC) $(SRCS) $(TESTS) runner.cpp stubs/stubs.cpp -lmbedcrypto -o $@

test: $(BUILD)/runner
	@bash gen_fixtures.sh $(BUILD)/fixtures
	@tests="$$($(BUILD)/runner --list)" || { echo "cannot run the test runner"; exit 1; }; \
	[ -n "$$tests" ] || { echo "no tests found"; exit 1; }; \
	passed=0; failed=0; \
	for t in $$tests; do \
	  if $(BUILD)/runner $$t $(BUILD)/fixtures; then passed=$$((passed+1)); else failed=$$((failed+1)); fi; \
	done; \
	echo "$$passed passed, $$failed failed"; \
	[ $$failed -eq 0 ] && [ $$passed -gt 0 ]

check:
	@mkdir -p $(BUILD)/devinc
	@cp ../../include/secrets.example.h $(BUILD)/devinc/secrets.h
	@for f in $(ALL_SRCS); do \
	  $(CXX) $(STD) $(WARN) -fsyntax-only -Istubs -I$(BUILD)/devinc -I../../include -I$(SRC) $$f || exit 1; \
	  $(CXX) $(STD) $(WARN) -fsyntax-only -DRELEASE_BUILD -Istubs -I../../include -I$(SRC) $$f || exit 1; \
	done; echo "syntax check passed (dev and release configurations)"

clean:
	rm -rf $(BUILD)
