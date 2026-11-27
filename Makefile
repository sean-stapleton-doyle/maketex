# maketex build, test, and release automation

BUILD_DIR         ?= build
RELEASE_BUILD_DIR ?= build-release
COVERAGE_BUILD_DIR ?= build-coverage
BUILD_TYPE        ?= Debug
PREFIX            ?= /usr/local
SANITIZE          ?=
COVERAGE          ?=
JOBS              ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
GENERATOR         ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

CMAKE_EXTRA_ARGS  ?=
ifneq ($(SANITIZE),)
  CMAKE_EXTRA_ARGS += -DMAKETEX_SANITIZE=$(SANITIZE)
endif
ifneq ($(COVERAGE),)
  CMAKE_EXTRA_ARGS += -DMAKETEX_COVERAGE=ON
endif

CPP_FILES = find src include cli test \( -name '*.cpp' -o -name '*.h' \) -print0

.DEFAULT_GOAL := build

.PHONY: help configure build release install test test-unit test-integration test-e2e \
        coverage format format-check clean tag-release

help: ## Show available targets
	@awk 'BEGIN {FS = ":.*## "; printf "\nUsage:\n  make <target> [VAR=value]\n\nTargets:\n"} \
	  /^[a-zA-Z0-9_-]+:.*## / { printf "  %-18s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@printf "\nVariables:\n"
	@printf "  BUILD_DIR          Debug/default build directory (default: %s)\n" "$(BUILD_DIR)"
	@printf "  RELEASE_BUILD_DIR  Release build directory (default: %s)\n" "$(RELEASE_BUILD_DIR)"
	@printf "  BUILD_TYPE         CMake build type for 'build' (default: %s)\n" "$(BUILD_TYPE)"
	@printf "  PREFIX             Install prefix for 'install' (default: %s)\n" "$(PREFIX)"
	@printf "  SANITIZE           Sanitizers, e.g. address,undefined (default: empty)\n"
	@printf "  VERSION            Release version for 'tag-release' (e.g. 0.2.0)\n"
	@printf "  PUSH               Set PUSH=1 with 'tag-release' to push branch & tag\n\n"

configure: ## Configure the default CMake build tree
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
	  cmake -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
	    -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" $(CMAKE_EXTRA_ARGS); \
	else \
	  cmake -S . -B "$(BUILD_DIR)" \
	    -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" $(CMAKE_EXTRA_ARGS); \
	fi

build: ## Build maketex and test targets (default: Debug)
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
	  cmake -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
	    -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" $(CMAKE_EXTRA_ARGS); \
	fi
	cmake --build "$(BUILD_DIR)" -j "$(JOBS)"

release: ## Configure and build an optimized Release binary in $(RELEASE_BUILD_DIR)
	@if [ ! -f "$(RELEASE_BUILD_DIR)/CMakeCache.txt" ]; then \
	  cmake -S . -B "$(RELEASE_BUILD_DIR)" -G "$(GENERATOR)" \
	    -DCMAKE_BUILD_TYPE=Release $(CMAKE_EXTRA_ARGS); \
	fi
	cmake --build "$(RELEASE_BUILD_DIR)" -j "$(JOBS)"

install: release ## Install the stripped Release binary to $(PREFIX)/bin/maketex
	cmake --install "$(RELEASE_BUILD_DIR)" --component maketex --prefix "$(PREFIX)" --strip

test: build ## Run the full test suite (unit + integration + e2e)
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure -j "$(JOBS)"

test-unit: build ## Run unit tests only (fast, no TeX engine needed)
	ctest --test-dir "$(BUILD_DIR)" --label-exclude "integration|e2e" --output-on-failure -j "$(JOBS)"

test-integration: build ## Run TeX engine integration tests
	ctest --test-dir "$(BUILD_DIR)" --label-regex integration --output-on-failure -j "$(JOBS)"

test-e2e: build ## Run CLI end-to-end tests
	ctest --test-dir "$(BUILD_DIR)" --label-regex e2e --output-on-failure -j "$(JOBS)"

coverage: ## Build with coverage, run tests, and write SonarQube coverage.xml (requires gcovr)
	@command -v gcovr >/dev/null 2>&1 || { echo "error: gcovr is required (brew install gcovr / apt install gcovr)" >&2; exit 1; }
	cmake -S . -B "$(COVERAGE_BUILD_DIR)" -G "$(GENERATOR)" \
	  -DCMAKE_BUILD_TYPE=Debug -DMAKETEX_COVERAGE=ON $(CMAKE_EXTRA_ARGS)
	cmake --build "$(COVERAGE_BUILD_DIR)" -j "$(JOBS)"
	ctest --test-dir "$(COVERAGE_BUILD_DIR)" --output-on-failure -j "$(JOBS)"
	@GCOV_BIN="$$(command -v gcov-14 >/dev/null 2>&1 && echo gcov-14 || (xcrun -f llvm-cov >/dev/null 2>&1 && echo 'xcrun llvm-cov gcov' || echo gcov))"; \
	gcovr --root . \
	  --filter 'src/' --filter 'include/' --filter 'cli/' \
	  --gcov-executable "$$GCOV_BIN" \
	  --exclude-throw-branches \
	  --exclude-unreachable-branches \
	  --sonarqube coverage.xml --print-summary \
	  "$(COVERAGE_BUILD_DIR)"

format: ## Format all C++ sources with clang-format
	@$(CPP_FILES) | xargs -0 clang-format -i

format-check: ## Verify formatting with clang-format (matches CI)
	@$(CPP_FILES) | xargs -0 clang-format --dry-run --Werror

clean: ## Remove build directories
	rm -rf "$(BUILD_DIR)" "$(RELEASE_BUILD_DIR)" "$(COVERAGE_BUILD_DIR)" coverage.xml

tag-release: ## Bump CMakeLists.txt version, commit, and tag v$(VERSION) (usage: make tag-release VERSION=0.2.0 [PUSH=1])
	@if [ -z "$(VERSION)" ]; then \
	  echo "error: VERSION is required (e.g. make tag-release VERSION=0.2.0)" >&2; \
	  exit 1; \
	fi
	@if ! printf '%s' "$(VERSION)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$$'; then \
	  echo "error: VERSION '$(VERSION)' must be semver (e.g. 0.2.0)" >&2; \
	  exit 1; \
	fi
	@if [ -n "$$(git status --porcelain)" ]; then \
	  echo "error: working tree is not clean; commit or stash changes first" >&2; \
	  exit 1; \
	fi
	@if git rev-parse -q --verify "refs/tags/v$(VERSION)" >/dev/null; then \
	  echo "error: tag v$(VERSION) already exists" >&2; \
	  exit 1; \
	fi
	$(MAKE) format-check
	$(MAKE) release
	ctest --test-dir "$(RELEASE_BUILD_DIR)" --label-exclude "integration|e2e" --output-on-failure -j "$(JOBS)"
	@current="$$(sed -n 's/^project(maketex VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)"; \
	if [ "$$current" != "$(VERSION)" ]; then \
	  perl -pi -e 's/^(project\(maketex VERSION )\S+/$${1}$(VERSION)/' CMakeLists.txt; \
	  git add CMakeLists.txt; \
	  git commit -m "Release v$(VERSION)"; \
	fi
	git tag -a "v$(VERSION)" -m "Release v$(VERSION)"
	@if [ "$(PUSH)" = "1" ]; then \
	  git push origin HEAD "v$(VERSION)"; \
	else \
	  echo "Tagged v$(VERSION). Push with: git push origin HEAD v$(VERSION)"; \
	fi
