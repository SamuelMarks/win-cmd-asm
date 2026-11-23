# Reproducible Multi-ISA, Multi-Assembler Build & Verification Orchestrator
# Supports GNU Make 3.81+ (macOS / Linux / MSYS2)
# Usage: make [all|lint|lower|verify|test|fuzz|coverage|clean] [TOOLCHAIN=all|nasm|fasm|jwasm] [TARGET=all|dos16|win16|x86_32|x86_64|arm32|arm64]

TOOLCHAIN ?= all
TARGET ?= all
BUILD_DIR ?= build
DIST_DIR ?= dist/src
DOS_OS ?= msdos

.PHONY: all lint lower build verify package test fuzz coverage ci-list ci-local ci-docker ci-dos-qemu hooks clean

all:
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh all

lint:
	@bash ./scripts/build-all.sh lint

lower:
	@DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh lower

build: lower
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh build

verify:
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh verify

package:
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh package

test:
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh test

fuzz:
	@TOOLCHAIN="$(TOOLCHAIN)" TARGET="$(TARGET)" BUILD_DIR="$(BUILD_DIR)" DIST_DIR="$(DIST_DIR)" bash ./scripts/build-all.sh fuzz

coverage:
	@cargo llvm-cov --workspace --summary-only

ci-list:
	@bash ./ci/replicate_ci.sh --list

ci-local:
	@bash ./ci/replicate_ci.sh --no-docker

ci-docker:
	@bash ./ci/replicate_ci.sh --only docker-ubuntu
	@bash ./ci/replicate_ci.sh --only docker-alpine

ci-dos-qemu:
	@sh ./ci/run_dos_qemu_matrix.sh --os "$(DOS_OS)" --toolchain nasm --build-dir "$(BUILD_DIR)"

hooks:
	@chmod +x .githooks/pre-commit
	@git config core.hooksPath .githooks

clean:
	@rm -rf "$(BUILD_DIR)" "$(DIST_DIR)" dist/release lcov.info
