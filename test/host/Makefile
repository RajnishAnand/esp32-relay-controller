CXX ?= g++
MBEDTLS_CFLAGS ?=
MBEDTLS_LIBS ?= -lmbedcrypto
BUILD ?= build
STD := -std=gnu++11
WARN := -Wall -Wextra -Wno-unused-parameter
SRC := ../../src
SRCS := $(filter-out $(SRC)/main.cpp $(SRC)/platform.cpp,$(wildcard $(SRC)/*.cpp))
TESTS := $(wildcard tests/*.cpp)
TEST_INC := -Istubs -Ifixtures -I../../include -I$(SRC) -I.
ALL_SRCS := $(wildcard $(SRC)/*.cpp)

.PHONY: all test check clean deps

all: deps test check

deps:
	@command -v openssl >/dev/null || { echo "missing: openssl"; exit 1; }
	@command -v python3 >/dev/null || { echo "missing: python3"; exit 1; }
	@echo '#include <mbedtls/pk.h>' | $(CXX) $(MBEDTLS_CFLAGS) -E -x c++ - >/dev/null 2>&1 || { \
	  echo "mbedtls development headers not found."; \
	  echo "  Debian/Ubuntu: sudo apt install libmbedtls-dev"; \
	  echo "  Fedora:        sudo dnf install mbedtls-devel"; \
	  echo "  Arch:          sudo pacman -S mbedtls"; \
	  echo "  macOS:         brew install mbedtls"; \
	  exit 1; }

$(BUILD)/runner: deps $(SRCS) $(TESTS) runner.cpp stubs/stubs.cpp testlib.h $(wildcard stubs/*.h) $(wildcard $(SRC)/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(STD) $(WARN) -g -DRELEASE_BUILD -DLOG_ENABLED=0 $(MBEDTLS_CFLAGS) $(TEST_INC) $(SRCS) $(TESTS) runner.cpp stubs/stubs.cpp $(MBEDTLS_LIBS) -o $@

test: deps $(BUILD)/runner
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
