SHELL := /bin/bash

MIGA80_TOOLCHAIN_PREFIX ?= $(HOME)/.local/m68k-amigaos
MIGA80_PIPX_BIN ?= $(HOME)/.local/bin

TARGET_CC := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-gcc
TARGET_SIZE := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-size
TARGET_NM := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-nm
TARGET_OBJDUMP := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-objdump
TARGET_AS := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-as
TARGET_OBJCOPY := $(MIGA80_TOOLCHAIN_PREFIX)/bin/m68k-amigaos-objcopy
TARGET_RUNTIME := -mcrt=nix20
HOST_CC ?= cc
PYTHON ?= python3

MIGA80_VERSION_FILE := VERSION
MIGA80_VERSION := $(strip $(shell /bin/cat $(MIGA80_VERSION_FILE)))
ifeq ($(MIGA80_VERSION),)
$(error $(MIGA80_VERSION_FILE) must contain the MIGA-80 release version)
endif

PROJECT_CPPFLAGS := -Isrc

AMIGA_BUILD_DIR := build/amiga
REPORT_DIR := build/reports
STAGING_DIR := build/staging
AMIGA_PROGRAM := $(AMIGA_BUILD_DIR)/miga80
AMIGA_MAP := $(AMIGA_BUILD_DIR)/miga80.map
STAGED_PROGRAM := $(STAGING_DIR)/miga80
SMOKE_BUILD_DIR := build/smoke
AGA_SCREEN_BUILD_DIR := $(SMOKE_BUILD_DIR)/aga-screen
AGA_SCREEN_SOURCE := tests/smoke/aga-screen/main.c
AGA_SCREEN_PROGRAM := $(AGA_SCREEN_BUILD_DIR)/program
AGA_SCREEN_MAP := $(AGA_SCREEN_BUILD_DIR)/program.map
AGA_SCREEN_EXPECTED := tests/smoke/aga-screen/expected.txt
C2P_REFERENCE_SOURCE := src/graphics/c2p_reference.c
C2P_REFERENCE_LAYOUTS_SOURCE := src/graphics/c2p_reference_layouts.c
C2P_REFERENCE_SOURCES := $(C2P_REFERENCE_SOURCE) $(C2P_REFERENCE_LAYOUTS_SOURCE)
C2P_REFERENCE_HEADER := src/graphics/c2p_reference.h
HOST_BUILD_DIR := build/host
MUSASHI_DEP_DIR := build/deps/musashi
MUSASHI_READY := $(MUSASHI_DEP_DIR)/.miga80-source-revision
COMPILER_ABI_SOURCE := compiler/abi/abi.c
COMPILER_ABI_HEADER := compiler/abi/abi.h compiler/abi/runtime.h
MIGA68K_TEST_BUILD_DIR := $(HOST_BUILD_DIR)/miga68k-test
MIGA68K_TEST_GENERATED_DIR := $(MIGA68K_TEST_BUILD_DIR)/generated
MIGA68K_TEST_GENERATED_STAMP := \
	$(MIGA68K_TEST_GENERATED_DIR)/.generated
MIGA68K_TEST_PROGRAM := $(MIGA68K_TEST_BUILD_DIR)/miga68k-test
MIGA68K_TEST_REPORT := $(REPORT_DIR)/miga68k-test-host.txt
MIGA68K_TEST_EXPECTED := tests/execute/miga68k-test.expected
MIGA68K_TEST_RUNNER_SOURCE := tools/miga68k-test/runner.c
MIGA68K_TEST_MEMORY_SOURCE := tools/miga68k-test/memory.c
MIGA68K_TEST_MEMORY_HEADER := tools/miga68k-test/memory.h
MIGA68K_TEST_CONFIG := tools/miga68k-test/miga80_musashi_config.h
MIGA68K_TEST_FIXTURE_SOURCE := tests/execute/mul_add.s
MIGA68K_TEST_FIXTURE_OBJECT := $(MIGA68K_TEST_BUILD_DIR)/mul_add.o
MIGA68K_TEST_FIXTURE_BINARY := $(MIGA68K_TEST_BUILD_DIR)/mul_add.bin
MUSASHI_GENERATOR := $(MIGA68K_TEST_BUILD_DIR)/m68kmake
MIGA68K_TEST_OBJECTS := \
	$(MIGA68K_TEST_BUILD_DIR)/runner.o \
	$(MIGA68K_TEST_BUILD_DIR)/memory.o \
	$(MIGA68K_TEST_BUILD_DIR)/abi.o \
	$(MIGA68K_TEST_BUILD_DIR)/m68kcpu.o \
	$(MIGA68K_TEST_BUILD_DIR)/m68kdasm.o \
	$(MIGA68K_TEST_BUILD_DIR)/m68kops.o \
	$(MIGA68K_TEST_BUILD_DIR)/softfloat.o
COMPILER_CPPFLAGS := -I.
COMPILER_FRONTEND_SOURCE := compiler/frontend/frontend.c
COMPILER_FRONTEND_HEADER := compiler/frontend/frontend.h
COMPILER_IR_SOURCE := compiler/ir/ir.c
COMPILER_IR_HEADER := compiler/ir/ir.h
COMPILER_VALUE_IR_SOURCE := compiler/value_ir/value_ir.c
COMPILER_VALUE_IR_HEADER := compiler/value_ir/value_ir.h
COMPILER_BACKEND_SOURCE := compiler/backend_m68k/backend.c
COMPILER_OPTIMIZED_BACKEND_SOURCE := compiler/backend_m68k/optimized.c
COMPILER_OPTIMIZED_INTERNAL_HEADER := \
	compiler/backend_m68k/optimized_internal.h
COMPILER_ENCODER_SOURCE := compiler/backend_m68k/encoder.c
COMPILER_BACKEND_HEADER := compiler/backend_m68k/backend.h
COMPILER_ENCODER_HEADER := compiler/backend_m68k/encoder.h
COMPILER_SOURCES := $(COMPILER_ABI_SOURCE) $(COMPILER_FRONTEND_SOURCE) \
	$(COMPILER_IR_SOURCE) $(COMPILER_VALUE_IR_SOURCE) \
	$(COMPILER_BACKEND_SOURCE) $(COMPILER_OPTIMIZED_BACKEND_SOURCE) \
	$(COMPILER_ENCODER_SOURCE)
COMPILER_HEADERS := $(COMPILER_ABI_HEADER) $(COMPILER_FRONTEND_HEADER) \
	$(COMPILER_IR_HEADER) $(COMPILER_VALUE_IR_HEADER) \
	$(COMPILER_BACKEND_HEADER) $(COMPILER_ENCODER_HEADER) \
	$(COMPILER_OPTIMIZED_INTERNAL_HEADER)
MIGA80C_SOURCE := tools/miga80c/main.c
MIGA80C_BUILD_DIR := $(HOST_BUILD_DIR)/miga80c
MIGA80C_PROGRAM := $(MIGA80C_BUILD_DIR)/miga80c
MIGA80C_AMIGA_BUILD_DIR := $(AMIGA_BUILD_DIR)/compiler-bootstrap
MIGA80C_AMIGA_PROGRAM := $(MIGA80C_AMIGA_BUILD_DIR)/miga80c
MIGA80C_AMIGA_MAP := $(MIGA80C_AMIGA_BUILD_DIR)/miga80c.map
MIGA80C_AMIGA_EXPECTED := tests/smoke/compiler-bootstrap/expected.txt
MIGA80C_AMIGA_REPORT := $(REPORT_DIR)/compiler-amiga-vamos.txt
MIGA80C_AMIGA_SIZE_REPORT := $(REPORT_DIR)/compiler-amiga-size.txt
MIGA80C_AMIGA_HOST_ASSEMBLY := $(MIGA80C_AMIGA_BUILD_DIR)/host-generated.s
MIGA80C_AMIGA_TARGET_ASSEMBLY := $(MIGA80C_AMIGA_BUILD_DIR)/amiga-generated.s
MIGA80C_AMIGA_SPILL_TEST := $(MIGA80C_AMIGA_BUILD_DIR)/spill-test
MIGA80C_AMIGA_HOST_SPILL_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-spill-generated.s
MIGA80C_AMIGA_TARGET_SPILL_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-spill-generated.s
MIGA80C_AMIGA_HOST_LOCALS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-locals-generated.s
MIGA80C_AMIGA_TARGET_LOCALS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-locals-generated.s
MIGA80C_AMIGA_HOST_CONDITIONALS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-conditionals-generated.s
MIGA80C_AMIGA_TARGET_CONDITIONALS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-conditionals-generated.s
MIGA80C_AMIGA_HOST_LOOPS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-loops-generated.s
MIGA80C_AMIGA_TARGET_LOOPS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-loops-generated.s
MIGA80C_AMIGA_HOST_LOOP_CONTROL_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-loop-control-generated.s
MIGA80C_AMIGA_TARGET_LOOP_CONTROL_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-loop-control-generated.s
MIGA80C_AMIGA_HOST_DIVISION_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-division-generated.s
MIGA80C_AMIGA_TARGET_DIVISION_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-division-generated.s
MIGA80C_AMIGA_HOST_NARROW_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-narrow-generated.s
MIGA80C_AMIGA_TARGET_NARROW_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-narrow-generated.s
MIGA80C_AMIGA_HOST_IMMUTABLE_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-immutable-generated.s
MIGA80C_AMIGA_TARGET_IMMUTABLE_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-immutable-generated.s
MIGA80C_AMIGA_HOST_FIXED_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-fixed-generated.s
MIGA80C_AMIGA_TARGET_FIXED_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-fixed-generated.s
MIGA80C_AMIGA_HOST_FIXED_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-fixed-eval.txt
MIGA80C_AMIGA_TARGET_FIXED_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-fixed-eval.txt
MIGA80C_AMIGA_HOST_FIXED_DIVISION_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-fixed-division-generated.s
MIGA80C_AMIGA_TARGET_FIXED_DIVISION_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-fixed-division-generated.s
MIGA80C_AMIGA_HOST_FIXED_DIVISION_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-fixed-division-eval.txt
MIGA80C_AMIGA_TARGET_FIXED_DIVISION_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-fixed-division-eval.txt
MIGA80C_AMIGA_HOST_CONVERSIONS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-conversions-generated.s
MIGA80C_AMIGA_TARGET_CONVERSIONS_ASSEMBLY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-conversions-generated.s
MIGA80C_AMIGA_HOST_CONVERSIONS_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-conversions-eval.txt
MIGA80C_AMIGA_TARGET_CONVERSIONS_EVAL := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-conversions-eval.txt
MIGA80C_AMIGA_ENCODER_TEST := \
	$(MIGA80C_AMIGA_BUILD_DIR)/encoder-test
MIGA80C_AMIGA_HOST_DIRECT_BINARY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-direct-o1.bin
MIGA80C_AMIGA_TARGET_DIRECT_BINARY := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-direct-o1.bin
MIGA80C_AMIGA_HOST_DIRECT_REPORT := \
	$(MIGA80C_AMIGA_BUILD_DIR)/host-direct-o1.txt
MIGA80C_AMIGA_TARGET_DIRECT_REPORT := \
	$(MIGA80C_AMIGA_BUILD_DIR)/amiga-direct-o1.txt
COMPILER_TEST_SOURCE := tests/host/compiler/main.c
COMPILER_TEST_EXPECTED := tests/host/compiler/expected.txt
COMPILER_TEST_BUILD_DIR := $(HOST_BUILD_DIR)/compiler
COMPILER_TEST_PROGRAM := $(COMPILER_TEST_BUILD_DIR)/test
COMPILER_TEST_REPORT := $(REPORT_DIR)/compiler-host.txt
COMPILER_ABI_TEST_SOURCE := tests/host/compiler-abi/main.c
COMPILER_ABI_TEST_EXPECTED := tests/host/compiler-abi/expected.txt
COMPILER_ABI_TEST_BUILD_DIR := $(HOST_BUILD_DIR)/compiler-abi
COMPILER_ABI_TEST_PROGRAM := $(COMPILER_ABI_TEST_BUILD_DIR)/test
COMPILER_ABI_TEST_REPORT := $(REPORT_DIR)/compiler-abi-host.txt
COMPILER_PIPELINE_SOURCE := tests/compile/arithmetic.lua
COMPILER_PIPELINE_EXPECTED := tests/compile/pipeline.expected
COMPILER_PIPELINE_SCRIPT := scripts/test-compiler-pipeline.sh
COMPILER_PIPELINE_BUILD_DIR := $(HOST_BUILD_DIR)/compiler-pipeline
COMPILER_PIPELINE_REPORT := $(REPORT_DIR)/compiler-pipeline-host.txt
COMPILER_VALUE_PIPELINE_SOURCE := tests/compile/value_ops.lua
COMPILER_VALUE_PIPELINE_EXPECTED := tests/compile/value_ops.expected
COMPILER_VALUE_PIPELINE_BUILD_DIR := $(HOST_BUILD_DIR)/compiler-value-pipeline
COMPILER_VALUE_PIPELINE_REPORT := $(REPORT_DIR)/compiler-value-pipeline-host.txt
COMPILER_REGISTER_PIPELINE_SOURCE := tests/compile/register_pressure.lua
COMPILER_REGISTER_PIPELINE_EXPECTED := tests/compile/register_pressure.expected
COMPILER_REGISTER_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-register-pipeline
COMPILER_REGISTER_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-register-pipeline-host.txt
COMPILER_LOCALS_PIPELINE_SOURCE := tests/compile/locals.lua
COMPILER_LOCALS_PIPELINE_EXPECTED := tests/compile/locals.expected
COMPILER_LOCALS_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-locals-pipeline
COMPILER_LOCALS_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-locals-pipeline-host.txt
COMPILER_CONDITIONALS_PIPELINE_SOURCE := tests/compile/conditionals.lua
COMPILER_CONDITIONALS_PIPELINE_EXPECTED := tests/compile/conditionals.expected
COMPILER_CONDITIONALS_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-conditionals-pipeline
COMPILER_CONDITIONALS_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-conditionals-pipeline-host.txt
COMPILER_LOOPS_PIPELINE_SOURCE := tests/compile/loops.lua
COMPILER_LOOPS_PIPELINE_EXPECTED := tests/compile/loops.expected
COMPILER_LOOPS_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-loops-pipeline
COMPILER_LOOPS_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-loops-pipeline-host.txt
COMPILER_LOOP_CONTROL_PIPELINE_SOURCE := tests/compile/loop_control.lua
COMPILER_LOOP_CONTROL_PIPELINE_EXPECTED := \
	tests/compile/loop_control.expected
COMPILER_LOOP_CONTROL_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-loop-control-pipeline
COMPILER_LOOP_CONTROL_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-loop-control-pipeline-host.txt
COMPILER_DIVISION_PIPELINE_SOURCE := tests/compile/division.lua
COMPILER_DIVISION_PIPELINE_EXPECTED := tests/compile/division.expected
COMPILER_DIVISION_PIPELINE_SCRIPT := scripts/test-compiler-division.sh
COMPILER_DIVISION_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-division-pipeline
COMPILER_DIVISION_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-division-pipeline-host.txt
COMPILER_NARROW_PIPELINE_SOURCE := tests/compile/narrow_integers.lua
COMPILER_NARROW_PIPELINE_EXPECTED := tests/compile/narrow_integers.expected
COMPILER_NARROW_PIPELINE_SCRIPT := scripts/test-compiler-narrow-integers.sh
COMPILER_NARROW_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-narrow-pipeline
COMPILER_NARROW_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-narrow-pipeline-host.txt
COMPILER_IMMUTABLE_PIPELINE_SOURCE := tests/compile/immutable_values.lua
COMPILER_IMMUTABLE_PIPELINE_EXPECTED := \
	tests/compile/immutable_values.expected
COMPILER_IMMUTABLE_PIPELINE_SCRIPT := \
	scripts/test-compiler-immutable-values.sh
COMPILER_IMMUTABLE_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-immutable-pipeline
COMPILER_IMMUTABLE_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-immutable-pipeline-host.txt
COMPILER_FIXED_PIPELINE_SOURCE := tests/compile/fixed_point.lua
COMPILER_FIXED_PIPELINE_EXPECTED := tests/compile/fixed_point.expected
COMPILER_FIXED_PIPELINE_SCRIPT := scripts/test-compiler-fixed-point.sh
COMPILER_FIXED_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-fixed-point-pipeline
COMPILER_FIXED_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-fixed-point-pipeline-host.txt
COMPILER_FIXED_DIVISION_PIPELINE_SOURCE := tests/compile/fixed_division.lua
COMPILER_FIXED_DIVISION_PIPELINE_EXPECTED := \
	tests/compile/fixed_division.expected
COMPILER_FIXED_DIVISION_PIPELINE_SCRIPT := \
	scripts/test-compiler-fixed-division.sh
COMPILER_FIXED_DIVISION_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-fixed-division-pipeline
COMPILER_FIXED_DIVISION_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-fixed-division-pipeline-host.txt
COMPILER_CONVERSIONS_PIPELINE_SOURCE := tests/compile/conversions.lua
COMPILER_CONVERSIONS_PIPELINE_EXPECTED := \
	tests/compile/conversions.expected
COMPILER_CONVERSIONS_PIPELINE_SCRIPT := \
	scripts/test-compiler-conversions.sh
COMPILER_CONVERSIONS_PIPELINE_BUILD_DIR := \
	$(HOST_BUILD_DIR)/compiler-conversions-pipeline
COMPILER_CONVERSIONS_PIPELINE_REPORT := \
	$(REPORT_DIR)/compiler-conversions-pipeline-host.txt
COMPILER_SPILL_SOURCE := tests/compile/spill_fixture.lua
COMPILER_SPILL_EXPECTED := tests/compile/spill_fixture.expected
COMPILER_SPILL_SCRIPT := scripts/test-compiler-spills.sh
COMPILER_SPILL_BUILD_DIR := $(HOST_BUILD_DIR)/compiler-spill
COMPILER_SPILL_REPORT := $(REPORT_DIR)/compiler-spill-host.txt
C2P_HOST_BUILD_DIR := $(HOST_BUILD_DIR)/c2p-reference
C2P_HOST_TEST_SOURCE := tests/host/c2p-reference/main.c
C2P_HOST_TEST_PROGRAM := $(C2P_HOST_BUILD_DIR)/test
C2P_HOST_TEST_EXPECTED := tests/host/c2p-reference/expected.txt
C2P_HOST_TEST_REPORT := $(REPORT_DIR)/c2p-reference-host.txt
C2P4_REFERENCE_SOURCE := src/graphics/c2p4_reference.c
C2P4_LOOKUP_SOURCE := src/graphics/c2p4_lookup.c
C2P4_MASK32_SOURCE := src/graphics/c2p4_mask32.c
C2P4_M68K_SOURCE := src/graphics/c2p4_m68k.c
C2P4_M68K_ASM_SOURCE := src/graphics/c2p4_m68k.S
C2P4_SOURCES := $(C2P4_REFERENCE_SOURCE) $(C2P4_LOOKUP_SOURCE) \
	$(C2P4_MASK32_SOURCE) \
	$(C2P4_M68K_SOURCE)
C2P4_TARGET_SOURCES := $(C2P4_SOURCES) $(C2P4_M68K_ASM_SOURCE)
C2P4_HEADER := src/graphics/c2p4_reference.h
C2P4_HOST_BUILD_DIR := $(HOST_BUILD_DIR)/c2p4-reference
C2P4_HOST_TEST_SOURCE := tests/host/c2p4-reference/main.c
C2P4_HOST_TEST_PROGRAM := $(C2P4_HOST_BUILD_DIR)/test
C2P4_HOST_TEST_EXPECTED := tests/host/c2p4-reference/expected.txt
C2P4_HOST_TEST_REPORT := $(REPORT_DIR)/c2p4-reference-host.txt
GRAPHICS_REFERENCE_SOURCE := src/graphics/reference_compositor.c
GRAPHICS_REFERENCE_HEADER := src/graphics/reference_compositor.h
GRAPHICS_REFERENCE_BUILD_DIR := $(HOST_BUILD_DIR)/graphics-reference
GRAPHICS_REFERENCE_TEST_SOURCE := tests/host/graphics-reference/main.c
GRAPHICS_REFERENCE_TEST_PROGRAM := $(GRAPHICS_REFERENCE_BUILD_DIR)/test
GRAPHICS_REFERENCE_TEST_EXPECTED := tests/host/graphics-reference/expected.txt
GRAPHICS_REFERENCE_TEST_REPORT := $(REPORT_DIR)/graphics-reference-host.txt
AGA_REFERENCE_SOURCE := src/graphics/aga_reference_decoder.c
AGA_REFERENCE_HEADER := src/graphics/aga_reference_decoder.h
AGA_REFERENCE_BUILD_DIR := $(HOST_BUILD_DIR)/aga-reference-decoder
AGA_REFERENCE_TEST_SOURCE := tests/host/aga-reference-decoder/main.c
AGA_REFERENCE_TEST_PROGRAM := $(AGA_REFERENCE_BUILD_DIR)/test
AGA_REFERENCE_TEST_EXPECTED := tests/host/aga-reference-decoder/expected.txt
AGA_REFERENCE_TEST_REPORT := $(REPORT_DIR)/aga-reference-decoder-host.txt
GRAPHICS_REPORT_TEST_SOURCE := tests/host/graphics-benchmark-report/test.sh
GRAPHICS_REPORT_TEST_EXPECTED := tests/host/graphics-benchmark-report/expected.txt
GRAPHICS_REPORT_TEST_REPORT := $(REPORT_DIR)/graphics-benchmark-report-host.txt
GRAPHICS_REPORT_VALIDATOR := scripts/validate-graphics-benchmark-report.sh
BENCHMARK_BUILD_DIR := build/benchmark
C2P_BENCHMARK_BUILD_DIR := $(BENCHMARK_BUILD_DIR)/c2p-layouts
C2P_BENCHMARK_SOURCE := tests/benchmark/c2p-layouts/main.c
C2P_BENCHMARK_PROGRAM := $(C2P_BENCHMARK_BUILD_DIR)/program
C2P_BENCHMARK_MAP := $(C2P_BENCHMARK_BUILD_DIR)/program.map
C2P_BENCHMARK_REPORT := $(REPORT_DIR)/c2p-layouts-fs-uae.txt
C2P4_BENCHMARK_BUILD_DIR := $(BENCHMARK_BUILD_DIR)/c2p4
C2P4_BENCHMARK_SOURCE := tests/benchmark/c2p4/main.c
C2P4_BENCHMARK_PROGRAM := $(C2P4_BENCHMARK_BUILD_DIR)/program
C2P4_BENCHMARK_MAP := $(C2P4_BENCHMARK_BUILD_DIR)/program.map
C2P4_BENCHMARK_REPORT := $(REPORT_DIR)/c2p4-fs-uae.txt
CHIPRAM_BENCHMARK_BUILD_DIR := $(BENCHMARK_BUILD_DIR)/chipram
CHIPRAM_BENCHMARK_SOURCE := tests/benchmark/chipram/main.c
CHIPRAM_BENCHMARK_ASM_SOURCE := tests/benchmark/chipram/kernels.S
CHIPRAM_BENCHMARK_HEADER := tests/benchmark/chipram/kernels.h
CHIPRAM_BENCHMARK_PROGRAM := $(CHIPRAM_BENCHMARK_BUILD_DIR)/program
CHIPRAM_BENCHMARK_MAP := $(CHIPRAM_BENCHMARK_BUILD_DIR)/program.map
CHIPRAM_BENCHMARK_REPORT := $(REPORT_DIR)/chipram-fs-uae.txt
CHIPRAM_REPORT_VALIDATOR := scripts/validate-chipram-benchmark-report.sh
CHIPRAM_REPORT_TEST_SOURCE := tests/host/chipram-benchmark-report/test.sh
CHIPRAM_REPORT_TEST_EXPECTED := tests/host/chipram-benchmark-report/expected.txt
CHIPRAM_REPORT_TEST_REPORT := $(REPORT_DIR)/chipram-benchmark-report-host.txt
EXCLUSIVE_GRAPHICS_BUILD_DIR := $(BENCHMARK_BUILD_DIR)/exclusive-graphics
EXCLUSIVE_GRAPHICS_SOURCE := tests/benchmark/exclusive-graphics/main.c
EXCLUSIVE_GRAPHICS_PROGRAM := $(EXCLUSIVE_GRAPHICS_BUILD_DIR)/program
EXCLUSIVE_GRAPHICS_MAP := $(EXCLUSIVE_GRAPHICS_BUILD_DIR)/program.map
EXCLUSIVE_GRAPHICS_REPORT := $(REPORT_DIR)/exclusive-graphics-fs-uae.txt
EXCLUSIVE_GRAPHICS_FAST_REPORT := \
	$(REPORT_DIR)/exclusive-graphics-fast-fs-uae.txt
EXCLUSIVE_GRAPHICS_REPORT_VALIDATOR := \
	scripts/validate-exclusive-graphics-benchmark-report.sh
EXCLUSIVE_GRAPHICS_REPORT_TEST_SOURCE := \
	tests/host/exclusive-graphics-benchmark-report/test.sh
EXCLUSIVE_GRAPHICS_REPORT_TEST_EXPECTED := \
	tests/host/exclusive-graphics-benchmark-report/expected.txt
EXCLUSIVE_GRAPHICS_REPORT_TEST_REPORT := \
	$(REPORT_DIR)/exclusive-graphics-benchmark-report-host.txt
EXCLUSIVE_GRAPHICS_PHYSICAL_BUILD_DIR := \
	$(BENCHMARK_BUILD_DIR)/exclusive-graphics-physical
EXCLUSIVE_GRAPHICS_PHYSICAL_PROGRAM := \
	$(EXCLUSIVE_GRAPHICS_PHYSICAL_BUILD_DIR)/program
EXCLUSIVE_GRAPHICS_PHYSICAL_MAP := \
	$(EXCLUSIVE_GRAPHICS_PHYSICAL_BUILD_DIR)/program.map
EXCLUSIVE_GRAPHICS_PHYSICAL_STARTUP := \
	tests/benchmark/exclusive-graphics/physical-startup-sequence
EXCLUSIVE_GRAPHICS_PHYSICAL_README := \
	tests/benchmark/exclusive-graphics/physical-readme.txt
EXCLUSIVE_GRAPHICS_ADF_BUILDER := \
	scripts/build-exclusive-graphics-test-adf.sh
EXCLUSIVE_GRAPHICS_ADF_TESTER := \
	scripts/test-exclusive-graphics-adf-fs-uae.sh
DISTRIBUTION_DIR := build/distribution
EXCLUSIVE_GRAPHICS_TEST_ADF := \
	$(DISTRIBUTION_DIR)/miga80-exclusive-graphics-test.adf
EXCLUSIVE_GRAPHICS_TEST_ADF_MANIFEST := \
	$(DISTRIBUTION_DIR)/miga80-exclusive-graphics-test.manifest.txt
FONT4X8_SOURCE_IMAGE := works/ID/font-4x8.png
FONT4X8_SOURCE_ORDER := works/ID/font-4x8.txt
FONT4X8_GENERATOR := scripts/generate-font-4x8.py
FONT4X8_GENERATED_DIR := build/generated
FONT4X8_GENERATED_HEADER := $(FONT4X8_GENERATED_DIR)/font4x8_data.h
FONT4X8_GENERATED_BINARY := $(FONT4X8_GENERATED_DIR)/FONT4X8.BIN
SOURCE_VIEW_SOURCE := src/ui/source_view.c
SOURCE_VIEW_HEADER := src/ui/source_view.h
SOURCE_VIEW_PALETTE_SOURCE := src/ui/palette.c
SOURCE_VIEW_PALETTE_HEADER := src/ui/palette.h
SOURCE_VIEW_FIXTURE := assets/demo/default.lua
SOURCE_VIEW_HOST_BUILD_DIR := $(HOST_BUILD_DIR)/source-view
SOURCE_VIEW_HOST_TEST_SOURCE := tests/host/source-view/main.c
SOURCE_VIEW_HOST_TEST_PROGRAM := $(SOURCE_VIEW_HOST_BUILD_DIR)/test
SOURCE_VIEW_HOST_EXPECTED := tests/host/source-view/expected.txt
SOURCE_VIEW_HOST_REPORT := $(REPORT_DIR)/source-view-host.txt
SOURCE_VIEW_HOST_PREVIEW := $(REPORT_DIR)/source-view.ppm
EDITOR_SOURCE := src/ui/editor.c
EDITOR_HEADER := src/ui/editor.h
EDITOR_HOST_TEST_SOURCE := tests/host/editor/main.c
EDITOR_HOST_TEST_PROGRAM := $(HOST_BUILD_DIR)/editor/test
EDITOR_HOST_REPORT := $(REPORT_DIR)/editor-host.txt
COMPILER_ENCODER_TEST_BUILD_DIR := $(HOST_BUILD_DIR)/compiler-encoder
COMPILER_ENCODER_TEST_SOURCE := tests/host/compiler-encoder/main.c
COMPILER_ENCODER_TEST_PROGRAM := $(COMPILER_ENCODER_TEST_BUILD_DIR)/test
COMPILER_ENCODER_TEST_EXPECTED := tests/host/compiler-encoder/expected.txt
COMPILER_ENCODER_TEST_REPORT := $(REPORT_DIR)/compiler-encoder-host.txt
COMPILER_ENCODER_TEST_BINARY := $(COMPILER_ENCODER_TEST_BUILD_DIR)/default.bin
COMPILER_ENCODER_O1_TEST_BINARY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default-o1-direct.bin
COMPILER_ENCODER_ASSEMBLY := $(COMPILER_ENCODER_TEST_BUILD_DIR)/default.s
COMPILER_ENCODER_ASSEMBLY_OBJECT := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default.o
COMPILER_ENCODER_ASSEMBLY_BINARY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default-assembly.bin
COMPILER_ENCODER_O1_ASSEMBLY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default-o1.s
COMPILER_ENCODER_O1_ASSEMBLY_OBJECT := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default-o1.o
COMPILER_ENCODER_O1_ASSEMBLY_BINARY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/default-o1-assembly.bin
COMPILER_ENCODER_MUSASHI_DIRECT_REPORT := \
	$(REPORT_DIR)/compiler-encoder-musashi-direct.txt
COMPILER_ENCODER_MUSASHI_ASSEMBLY_REPORT := \
	$(REPORT_DIR)/compiler-encoder-musashi-assembly.txt
COMPILER_ENCODER_MUSASHI_O1_DIRECT_REPORT := \
	$(REPORT_DIR)/compiler-encoder-musashi-o1-direct.txt
COMPILER_ENCODER_MUSASHI_O1_ASSEMBLY_REPORT := \
	$(REPORT_DIR)/compiler-encoder-musashi-o1-assembly.txt
COMPILER_ENCODER_MUSASHI_DIRECT_EXPECTED := \
	tests/execute/mandelbrot-direct.expected
COMPILER_ENCODER_MUSASHI_ASSEMBLY_EXPECTED := \
	tests/execute/mandelbrot-assembly.expected
COMPILER_ENCODER_MUSASHI_O1_EXPECTED := \
	tests/execute/mandelbrot-o1.expected
COMPILER_CALL_FIXTURE := tests/compile/call-survival.lua
COMPILER_CALL_DIRECT_BINARY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/call-survival-direct.bin
COMPILER_CALL_ASSEMBLY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/call-survival.s
COMPILER_CALL_ASSEMBLY_OBJECT := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/call-survival.o
COMPILER_CALL_ASSEMBLY_BINARY := \
	$(COMPILER_ENCODER_TEST_BUILD_DIR)/call-survival-assembly.bin
COMPILER_CALL_ENCODER_REPORT := \
	$(REPORT_DIR)/compiler-call-survival-encoder.txt
COMPILER_CALL_DIRECT_REPORT := \
	$(REPORT_DIR)/compiler-call-survival-direct.txt
COMPILER_CALL_ASSEMBLY_REPORT := \
	$(REPORT_DIR)/compiler-call-survival-assembly.txt
COMPILER_CALL_ENCODER_EXPECTED := \
	tests/host/compiler-encoder/call-survival.expected
COMPILER_CALL_MUSASHI_EXPECTED := \
	tests/execute/call-survival.expected
BOOT_LOGO_HEADER := build/generated/boot_logo_data.h
BOOT_JINGLE_OBJECT := build/amiga/source-view/boot_jingle.o
MIGA80_DEMO_BUILD_DIR := $(AMIGA_BUILD_DIR)/source-view
PTPLAYER_OBJECT := $(AMIGA_BUILD_DIR)/source-view/ptplayer.o
MIGA80_DEMO_SOURCE := src/demo/music_host.c src/audio/mod.c src/demo/intro.c src/demo/intro_effect.c src/demo/file_picker.c src/demo/main.c src/demo/supervisor.c src/demo/stop_test.c $(EDITOR_SOURCE) \
	src/demo/drawing_host.c src/demo/animation.c src/graphics/drawing.c src/graphics/triangle.c src/graphics/c2p4_reference.c \
	src/graphics/c2p4_m68k.c src/graphics/c2p4_kalms.c
MIGA80_DEMO_HEADERS := src/demo/music_host.h src/audio/mod.h src/demo/intro.h src/demo/intro_effect.h src/audio/boot_jingle.h src/demo/file_picker.h src/demo/supervisor.h src/demo/stop_test.h $(EDITOR_HEADER) \
	src/demo/drawing_host.h src/demo/animation.h src/graphics/drawing.h src/graphics/c2p4_reference.h
MIGA80_DEMO_RUNTIME_SOURCE := src/demo/music_bridge.S src/demo/runtime_guarded.S src/demo/drawing_bridge.S src/graphics/triangle_m68k.S \
	src/graphics/c2p4_m68k.S src/graphics/c2p4_kalms.S
MIGA80_DEMO_COMPILER_SOURCES := $(COMPILER_ABI_SOURCE) \
	$(COMPILER_FRONTEND_SOURCE) $(COMPILER_IR_SOURCE) \
	$(COMPILER_VALUE_IR_SOURCE) $(COMPILER_BACKEND_SOURCE) \
	$(COMPILER_OPTIMIZED_BACKEND_SOURCE) $(COMPILER_ENCODER_SOURCE)
MIGA80_DEMO_PROGRAM := $(MIGA80_DEMO_BUILD_DIR)/miga80
MIGA80_DEMO_MAP := $(MIGA80_DEMO_BUILD_DIR)/miga80.map
MIGA80_DEMO_SIZE_REPORT := $(REPORT_DIR)/source-view-amiga-size.txt
MIGA80_DEMO_STARTUP := assets/demo/Startup-Sequence
MIGA80_DEMO_README := assets/demo/README.TXT
MIGA80_DEMO_ADF_BUILDER := scripts/build-miga80-demo-adf.sh
MIGA80_DEMO_ADF_TESTER := scripts/test-miga80-demo-adf-fs-uae.sh
MIGA80_DEMO_ADF_EXPECTED := tests/smoke/source-view-adf/expected.txt
MIGA80_DEMO_ADF_AUTORUN_EXPECTED := \
	tests/smoke/source-view-adf/autorun-expected.txt
MIGA80_DEMO_ADF := $(DISTRIBUTION_DIR)/miga80-source-view.adf
MIGA80_DEMO_ADF_MANIFEST := \
	$(DISTRIBUTION_DIR)/miga80-source-view.manifest.txt

TARGET_CFLAGS := \
	-std=c99 \
	-m68020 \
	-msoft-float \
	-Os \
	-Wall \
	-Wextra \
	-Werror \
	-fno-common \
	-ffunction-sections \
	-fdata-sections

HOST_CFLAGS := \
	-std=c99 \
	-O2 \
	-Wall \
	-Wextra \
	-Werror \
	-pedantic

MUSASHI_CONFIG_DEFINE := \
	-DMUSASHI_CNF=\"miga80_musashi_config.h\"
MUSASHI_CPPFLAGS := \
	-I. \
	-Itools/miga68k-test \
	-I$(MUSASHI_DEP_DIR) \
	-I$(MIGA68K_TEST_GENERATED_DIR)
MUSASHI_CFLAGS := -std=c99 -O2 -w

C2P_BENCHMARK_CFLAGS = $(filter-out -Os,$(TARGET_CFLAGS)) -O2

.DELETE_ON_ERROR:

.PHONY: all amiga stage inspect vamos-test fs-uae-smoke c2p-test \
	c2p4-test graphics-reference-test aga-reference-test \
	miga68k-test miga80c compiler-abi-test compiler-test \
	compiler-execute-test compiler-spill-test compiler-amiga-test \
	graphics-report-test chipram-report-test \
	exclusive-graphics-report-test \
	aga-screen aga-screen-inspect aga-screen-smoke c2p-benchmark \
	c2p-benchmark-inspect c2p-benchmark-fs-uae c2p4-benchmark \
	c2p4-benchmark-inspect c2p4-benchmark-fs-uae chipram-benchmark \
	chipram-benchmark-inspect chipram-benchmark-fs-uae \
	exclusive-graphics-benchmark exclusive-graphics-benchmark-inspect \
	exclusive-graphics-benchmark-fs-uae \
	exclusive-graphics-benchmark-fs-uae-fast exclusive-graphics-test-adf \
	exclusive-graphics-test-adf-inspect exclusive-graphics-test-adf-fs-uae \
	source-view-test editor-test compiler-encoder-test compiler-encoder-musashi-test \
	compiler-call-test \
	miga80-demo miga80-demo-inspect miga80-demo-adf \
	miga80-demo-adf-inspect miga80-demo-adf-fs-uae \
	miga80-demo-adf-fs-uae-autorun \
	runtime-compare \
	check run clean

all: amiga

amiga: $(AMIGA_PROGRAM)

$(AMIGA_PROGRAM): src/main.c Makefile
	@mkdir -p $(AMIGA_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(TARGET_CFLAGS) $< -Wl,-Map,$(AMIGA_MAP) -o $@ $(TARGET_RUNTIME)

stage: $(STAGED_PROGRAM)

$(STAGED_PROGRAM): $(AMIGA_PROGRAM)
	@mkdir -p $(STAGING_DIR)
	cp $< $@
	chmod 755 $@

inspect: $(AMIGA_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(AMIGA_PROGRAM) | tee $(REPORT_DIR)/miga80-size.txt
	$(TARGET_NM) --print-size --size-sort $(AMIGA_PROGRAM) >$(REPORT_DIR)/miga80-symbols.txt
	$(TARGET_OBJDUMP) -dr $(AMIGA_PROGRAM) >$(REPORT_DIR)/miga80-disassembly.txt

vamos-test: $(AMIGA_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 $(AMIGA_PROGRAM) >$(REPORT_DIR)/miga80-vamos.txt
	diff -u tests/smoke/hosted-bootstrap/expected.txt $(REPORT_DIR)/miga80-vamos.txt

run: stage
	./scripts/run-fs-uae.sh a1200-pal-ks30-hd

$(FONT4X8_GENERATED_HEADER): $(FONT4X8_SOURCE_IMAGE) \
		$(FONT4X8_SOURCE_ORDER) $(FONT4X8_GENERATOR)
	@mkdir -p $(FONT4X8_GENERATED_DIR)
	$(PYTHON) $(FONT4X8_GENERATOR) $(FONT4X8_SOURCE_IMAGE) \
		$(FONT4X8_SOURCE_ORDER) --header $@

$(FONT4X8_GENERATED_BINARY): $(FONT4X8_SOURCE_IMAGE) \
		$(FONT4X8_SOURCE_ORDER) $(FONT4X8_GENERATOR)
	@mkdir -p $(FONT4X8_GENERATED_DIR)
	$(PYTHON) $(FONT4X8_GENERATOR) $(FONT4X8_SOURCE_IMAGE) \
		$(FONT4X8_SOURCE_ORDER) --binary $@

$(SOURCE_VIEW_HOST_TEST_PROGRAM): $(SOURCE_VIEW_HOST_TEST_SOURCE) \
		$(SOURCE_VIEW_SOURCE) $(SOURCE_VIEW_HEADER) \
		$(SOURCE_VIEW_PALETTE_SOURCE) $(SOURCE_VIEW_PALETTE_HEADER) \
		$(FONT4X8_GENERATED_HEADER) Makefile
	@mkdir -p $(SOURCE_VIEW_HOST_BUILD_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) -I$(FONT4X8_GENERATED_DIR) \
		$(HOST_CFLAGS) $(SOURCE_VIEW_HOST_TEST_SOURCE) \
		$(SOURCE_VIEW_SOURCE) $(SOURCE_VIEW_PALETTE_SOURCE) -o $@

source-view-test: $(SOURCE_VIEW_HOST_TEST_PROGRAM) $(SOURCE_VIEW_FIXTURE) \
		$(SOURCE_VIEW_HOST_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(SOURCE_VIEW_HOST_TEST_PROGRAM) $(SOURCE_VIEW_FIXTURE) \
		$(SOURCE_VIEW_HOST_PREVIEW) >$(SOURCE_VIEW_HOST_REPORT)
	diff -u $(SOURCE_VIEW_HOST_EXPECTED) $(SOURCE_VIEW_HOST_REPORT)

$(EDITOR_HOST_TEST_PROGRAM): $(EDITOR_HOST_TEST_SOURCE) $(EDITOR_SOURCE) \
		$(EDITOR_HEADER) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) \
		-fsanitize=address,undefined $(EDITOR_HOST_TEST_SOURCE) \
		$(EDITOR_SOURCE) -o $@

editor-test: $(EDITOR_HOST_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(EDITOR_HOST_TEST_PROGRAM) >$(EDITOR_HOST_REPORT)
	cat $(EDITOR_HOST_REPORT)

compiler-encoder-test: $(COMPILER_ENCODER_TEST_PROGRAM) \
		$(SOURCE_VIEW_FIXTURE) $(COMPILER_ENCODER_TEST_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(COMPILER_ENCODER_TEST_PROGRAM) $(SOURCE_VIEW_FIXTURE) \
		$(COMPILER_ENCODER_TEST_BINARY) $(COMPILER_ENCODER_O1_TEST_BINARY) \
		>$(COMPILER_ENCODER_TEST_REPORT)
	diff -u $(COMPILER_ENCODER_TEST_EXPECTED) \
		$(COMPILER_ENCODER_TEST_REPORT)

$(COMPILER_ENCODER_TEST_PROGRAM): $(COMPILER_ENCODER_TEST_SOURCE) \
		$(COMPILER_SOURCES) $(COMPILER_HEADERS) Makefile
	@mkdir -p $(COMPILER_ENCODER_TEST_BUILD_DIR)
	$(HOST_CC) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) $(COMPILER_SOURCES) \
		$(COMPILER_ENCODER_TEST_SOURCE) -o $@

$(COMPILER_ENCODER_ASSEMBLY): $(MIGA80C_PROGRAM) $(SOURCE_VIEW_FIXTURE)
	@mkdir -p $(COMPILER_ENCODER_TEST_BUILD_DIR)
	$(MIGA80C_PROGRAM) $(SOURCE_VIEW_FIXTURE) -O0 -S -o $@

$(COMPILER_ENCODER_ASSEMBLY_OBJECT): $(COMPILER_ENCODER_ASSEMBLY)
	$(TARGET_AS) -m68020 $< -o $@

$(COMPILER_ENCODER_ASSEMBLY_BINARY): \
		$(COMPILER_ENCODER_ASSEMBLY_OBJECT)
	$(TARGET_OBJCOPY) -O binary -j .text $< $@

$(COMPILER_ENCODER_O1_ASSEMBLY): $(MIGA80C_PROGRAM) $(SOURCE_VIEW_FIXTURE)
	@mkdir -p $(COMPILER_ENCODER_TEST_BUILD_DIR)
	$(MIGA80C_PROGRAM) $(SOURCE_VIEW_FIXTURE) -O1 -S -o $@

$(COMPILER_ENCODER_O1_ASSEMBLY_OBJECT): $(COMPILER_ENCODER_O1_ASSEMBLY)
	$(TARGET_AS) -m68020 $< -o $@

$(COMPILER_ENCODER_O1_ASSEMBLY_BINARY): \
		$(COMPILER_ENCODER_O1_ASSEMBLY_OBJECT)
	$(TARGET_OBJCOPY) -O binary -j .text $< $@

compiler-encoder-musashi-test: compiler-encoder-test $(MIGA68K_TEST_PROGRAM) \
		$(COMPILER_ENCODER_ASSEMBLY_BINARY) \
		$(COMPILER_ENCODER_O1_ASSEMBLY_BINARY) \
		$(COMPILER_ENCODER_MUSASHI_DIRECT_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_ASSEMBLY_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_O1_EXPECTED)
	$(MIGA68K_TEST_PROGRAM) --pset $(COMPILER_ENCODER_TEST_BINARY) \
		0xc4604fc7 20480 >$(COMPILER_ENCODER_MUSASHI_DIRECT_REPORT)
	diff -u $(COMPILER_ENCODER_MUSASHI_DIRECT_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_DIRECT_REPORT)
	$(MIGA68K_TEST_PROGRAM) --pset $(COMPILER_ENCODER_ASSEMBLY_BINARY) \
		0xc4604fc7 20480 >$(COMPILER_ENCODER_MUSASHI_ASSEMBLY_REPORT)
	diff -u $(COMPILER_ENCODER_MUSASHI_ASSEMBLY_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_ASSEMBLY_REPORT)
	cmp $(COMPILER_ENCODER_O1_TEST_BINARY) \
		$(COMPILER_ENCODER_O1_ASSEMBLY_BINARY)
	$(MIGA68K_TEST_PROGRAM) --pset $(COMPILER_ENCODER_O1_TEST_BINARY) \
		0xc4604fc7 20480 >$(COMPILER_ENCODER_MUSASHI_O1_DIRECT_REPORT)
	diff -u $(COMPILER_ENCODER_MUSASHI_O1_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_O1_DIRECT_REPORT)
	$(MIGA68K_TEST_PROGRAM) --pset $(COMPILER_ENCODER_O1_ASSEMBLY_BINARY) \
		0xc4604fc7 20480 >$(COMPILER_ENCODER_MUSASHI_O1_ASSEMBLY_REPORT)
	diff -u $(COMPILER_ENCODER_MUSASHI_O1_EXPECTED) \
		$(COMPILER_ENCODER_MUSASHI_O1_ASSEMBLY_REPORT)

$(COMPILER_CALL_ASSEMBLY): $(MIGA80C_PROGRAM) $(COMPILER_CALL_FIXTURE)
	@mkdir -p $(COMPILER_ENCODER_TEST_BUILD_DIR)
	$(MIGA80C_PROGRAM) $(COMPILER_CALL_FIXTURE) -O1 -S -o $@

$(COMPILER_CALL_ASSEMBLY_OBJECT): $(COMPILER_CALL_ASSEMBLY)
	$(TARGET_AS) -m68020 $< -o $@

$(COMPILER_CALL_ASSEMBLY_BINARY): $(COMPILER_CALL_ASSEMBLY_OBJECT)
	$(TARGET_OBJCOPY) -O binary -j .text $< $@

compiler-call-test: $(COMPILER_ENCODER_TEST_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_CALL_ASSEMBLY_BINARY) \
		$(COMPILER_CALL_ENCODER_EXPECTED) \
		$(COMPILER_CALL_MUSASHI_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(COMPILER_ENCODER_TEST_PROGRAM) --o1 $(COMPILER_CALL_FIXTURE) \
		$(COMPILER_CALL_DIRECT_BINARY) >$(COMPILER_CALL_ENCODER_REPORT)
	diff -u $(COMPILER_CALL_ENCODER_EXPECTED) \
		$(COMPILER_CALL_ENCODER_REPORT)
	cmp $(COMPILER_CALL_DIRECT_BINARY) $(COMPILER_CALL_ASSEMBLY_BINARY)
	$(MIGA68K_TEST_PROGRAM) --pset-case $(COMPILER_CALL_DIRECT_BINARY) \
		call-survival 7 5 3 12 1 >$(COMPILER_CALL_DIRECT_REPORT)
	diff -u $(COMPILER_CALL_MUSASHI_EXPECTED) \
		$(COMPILER_CALL_DIRECT_REPORT)
	$(MIGA68K_TEST_PROGRAM) --pset-case $(COMPILER_CALL_ASSEMBLY_BINARY) \
		call-survival 7 5 3 12 1 >$(COMPILER_CALL_ASSEMBLY_REPORT)
	diff -u $(COMPILER_CALL_MUSASHI_EXPECTED) \
		$(COMPILER_CALL_ASSEMBLY_REPORT)

.PHONY: runtime-guards-test miga80-demo-adf-fs-uae-workflow
runtime-guards-test: $(COMPILER_ENCODER_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-runtime-guards.py $(COMPILER_ENCODER_TEST_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) \
		>$(REPORT_DIR)/runtime-guards-host.txt
	@cat $(REPORT_DIR)/runtime-guards-host.txt

miga80-demo-adf-fs-uae-workflow: $(MIGA80_DEMO_ADF)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=600 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		tests/smoke/source-view-adf/workflow-expected.txt SELFTEST

.PHONY: miga80-demo-adf-fs-uae-stop miga80-demo-adf-fs-uae-direct
miga80-demo-adf-fs-uae-stop: $(MIGA80_DEMO_ADF)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=480 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		tests/smoke/source-view-adf/stop-expected.txt STOPTEST

miga80-demo-adf-fs-uae-direct: $(MIGA80_DEMO_ADF)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=180 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		tests/smoke/source-view-adf/direct-expected.txt AUTORUN_DIRECT

DRAWING_TEST_PROGRAM := $(HOST_BUILD_DIR)/drawing/test
DRAWING_TEST_HEADER := build/generated/drawing_test_data.h

$(DRAWING_TEST_PROGRAM): tests/host/drawing/main.c tests/host/drawing/triangles.c src/graphics/drawing.c src/graphics/triangle.c \
        src/graphics/drawing.h $(COMPILER_SOURCES) $(COMPILER_HEADERS) \
        $(COMPILER_ENCODER_SOURCE) $(COMPILER_ENCODER_HEADER) compiler/abi/runtime.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) \
		-fsanitize=address,undefined tests/host/drawing/main.c tests/host/drawing/triangles.c src/graphics/drawing.c src/graphics/triangle.c \
		$(COMPILER_SOURCES) -o $@

$(DRAWING_TEST_HEADER): $(DRAWING_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
        tests/runtime/layers.lua scripts/test-drawing.py
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-drawing.py $(DRAWING_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) >$(REPORT_DIR)/drawing-host.txt
	@cat $(REPORT_DIR)/drawing-host.txt

.PHONY: drawing-test miga80-demo-adf-fs-uae-graphics
drawing-test: $(DRAWING_TEST_HEADER)

miga80-demo-adf-fs-uae-graphics: $(MIGA80_DEMO_ADF)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=180 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		tests/smoke/source-view-adf/graphics-expected.txt GRAPHICSTEST

miga80-demo: $(MIGA80_DEMO_PROGRAM)

$(AMIGA_BUILD_DIR)/source-view/ptplayer.inc: third_party/ptplayer/ptplayer.asm scripts/prepare-ptplayer.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/prepare-ptplayer.py $@

$(PTPLAYER_OBJECT): src/audio/ptplayer_host.asm $(AMIGA_BUILD_DIR)/source-view/ptplayer.inc
	$(MIGA80_TOOLCHAIN_PREFIX)/bin/vasmm68k_mot -quiet -Fhunk -m68000 -o $@ $<

$(MIGA80_DEMO_PROGRAM): $(PTPLAYER_OBJECT) $(BOOT_LOGO_HEADER) $(BOOT_JINGLE_OBJECT) $(DRAWING_TEST_HEADER) $(MIGA80_DEMO_SOURCE) $(MIGA80_DEMO_HEADERS) $(SOURCE_VIEW_SOURCE) \
		$(MIGA80_DEMO_RUNTIME_SOURCE) $(MIGA80_DEMO_COMPILER_SOURCES) \
		$(COMPILER_ABI_HEADER) $(COMPILER_FRONTEND_HEADER) \
		$(COMPILER_IR_HEADER) $(COMPILER_VALUE_IR_HEADER) \
		$(COMPILER_BACKEND_HEADER) $(COMPILER_ENCODER_HEADER) \
		$(COMPILER_OPTIMIZED_INTERNAL_HEADER) \
		$(SOURCE_VIEW_HEADER) $(SOURCE_VIEW_PALETTE_SOURCE) \
		$(SOURCE_VIEW_PALETTE_HEADER) $(FONT4X8_GENERATED_HEADER) \
		$(C2P_REFERENCE_SOURCE) $(C2P_REFERENCE_HEADER) Makefile
	@mkdir -p $(MIGA80_DEMO_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(COMPILER_CPPFLAGS) \
		-I$(FONT4X8_GENERATED_DIR) \
		$(TARGET_CFLAGS) $(MIGA80_DEMO_SOURCE) $(SOURCE_VIEW_SOURCE) \
		$(SOURCE_VIEW_PALETTE_SOURCE) $(C2P_REFERENCE_SOURCE) \
		$(MIGA80_DEMO_COMPILER_SOURCES) $(MIGA80_DEMO_RUNTIME_SOURCE) $(BOOT_JINGLE_OBJECT) $(PTPLAYER_OBJECT) \
		-Wl,--gc-sections -Wl,-Map,$(MIGA80_DEMO_MAP) -o $@ $(TARGET_RUNTIME)

miga80-demo-inspect: $(MIGA80_DEMO_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(MIGA80_DEMO_PROGRAM) | tee $(MIGA80_DEMO_SIZE_REPORT)
	$(TARGET_NM) --print-size --size-sort $(MIGA80_DEMO_PROGRAM) \
		>$(REPORT_DIR)/source-view-amiga-symbols.txt
	$(TARGET_OBJDUMP) -dr $(MIGA80_DEMO_PROGRAM) \
		>$(REPORT_DIR)/source-view-amiga-disassembly.txt

miga80-demo-adf: $(MIGA80_DEMO_ADF)

$(MIGA80_DEMO_ADF): $(MIGA80_DEMO_PROGRAM) $(SOURCE_VIEW_FIXTURE) \
		$(FONT4X8_GENERATED_BINARY) $(MIGA80_DEMO_STARTUP) \
		$(MIGA80_DEMO_README) LICENSE assets/demo/layers.lua assets/demo/cube.lua $(MIGA80_DEMO_ADF_BUILDER)
	$(MIGA80_DEMO_ADF_BUILDER) $(MIGA80_DEMO_PROGRAM) \
		$(SOURCE_VIEW_FIXTURE) $(FONT4X8_GENERATED_BINARY) \
		$(MIGA80_DEMO_STARTUP) $(MIGA80_DEMO_README) LICENSE $@ assets/demo/layers.lua assets/demo/cube.lua

miga80-demo-adf-inspect: $(MIGA80_DEMO_ADF)
	xdfscan $(MIGA80_DEMO_ADF)
	xdftool $(MIGA80_DEMO_ADF) list
	@printf 'Manifest: %s\n' $(MIGA80_DEMO_ADF_MANIFEST)

miga80-demo-adf-fs-uae: $(MIGA80_DEMO_ADF) \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF_EXPECTED)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=45 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		$(MIGA80_DEMO_ADF_EXPECTED)

miga80-demo-adf-fs-uae-autorun: $(MIGA80_DEMO_ADF) \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF_AUTORUN_EXPECTED)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=180 \
		$(MIGA80_DEMO_ADF_TESTER) $(MIGA80_DEMO_ADF) \
		$(MIGA80_DEMO_ADF_AUTORUN_EXPECTED) AUTORUN

fs-uae-smoke: stage
	./scripts/test-fs-uae-runtime.sh

miga68k-test: $(MIGA68K_TEST_PROGRAM) $(MIGA68K_TEST_FIXTURE_BINARY) \
		$(MIGA68K_TEST_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(MIGA68K_TEST_PROGRAM) $(MIGA68K_TEST_FIXTURE_BINARY) \
		>$(MIGA68K_TEST_REPORT)
	diff -u $(MIGA68K_TEST_EXPECTED) $(MIGA68K_TEST_REPORT)

$(MUSASHI_READY): scripts/fetch-musashi.sh toolchain/versions.lock
	./scripts/fetch-musashi.sh $(MUSASHI_DEP_DIR)

$(MUSASHI_GENERATOR): $(MUSASHI_READY)
	@mkdir -p $(MIGA68K_TEST_BUILD_DIR)
	$(HOST_CC) $(MUSASHI_CFLAGS) $(MUSASHI_DEP_DIR)/m68kmake.c -o $@

$(MIGA68K_TEST_GENERATED_STAMP): $(MUSASHI_GENERATOR) $(MUSASHI_READY)
	@mkdir -p $(MIGA68K_TEST_GENERATED_DIR)
	$(MUSASHI_GENERATOR) $(MIGA68K_TEST_GENERATED_DIR) \
		$(MUSASHI_DEP_DIR)/m68k_in.c
	@touch $@

$(MIGA68K_TEST_BUILD_DIR)/runner.o: $(MIGA68K_TEST_RUNNER_SOURCE) \
		$(MIGA68K_TEST_MEMORY_HEADER) $(MIGA68K_TEST_CONFIG) \
		$(COMPILER_ABI_HEADER) \
		$(MUSASHI_READY)
	@mkdir -p $(MIGA68K_TEST_BUILD_DIR)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(HOST_CFLAGS) -c $(MIGA68K_TEST_RUNNER_SOURCE) -o $@

$(MIGA68K_TEST_BUILD_DIR)/memory.o: $(MIGA68K_TEST_MEMORY_SOURCE) \
		$(MIGA68K_TEST_MEMORY_HEADER) $(MIGA68K_TEST_CONFIG) \
		$(MUSASHI_READY)
	@mkdir -p $(MIGA68K_TEST_BUILD_DIR)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(HOST_CFLAGS) -c $(MIGA68K_TEST_MEMORY_SOURCE) -o $@

$(MIGA68K_TEST_BUILD_DIR)/abi.o: $(COMPILER_ABI_SOURCE) \
		$(COMPILER_ABI_HEADER) Makefile
	@mkdir -p $(MIGA68K_TEST_BUILD_DIR)
	$(HOST_CC) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) \
		-c $(COMPILER_ABI_SOURCE) -o $@

$(MIGA68K_TEST_BUILD_DIR)/m68kcpu.o: $(MUSASHI_READY) \
		$(MIGA68K_TEST_GENERATED_STAMP) $(MIGA68K_TEST_CONFIG)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(MUSASHI_CFLAGS) -c $(MUSASHI_DEP_DIR)/m68kcpu.c -o $@

$(MIGA68K_TEST_BUILD_DIR)/m68kdasm.o: $(MUSASHI_READY) \
		$(MIGA68K_TEST_CONFIG)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(MUSASHI_CFLAGS) -c $(MUSASHI_DEP_DIR)/m68kdasm.c -o $@

$(MIGA68K_TEST_BUILD_DIR)/m68kops.o: $(MUSASHI_READY) \
		$(MIGA68K_TEST_GENERATED_STAMP) $(MIGA68K_TEST_CONFIG)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(MUSASHI_CFLAGS) -c $(MIGA68K_TEST_GENERATED_DIR)/m68kops.c \
		-o $@

$(MIGA68K_TEST_BUILD_DIR)/softfloat.o: $(MUSASHI_READY) \
		$(MIGA68K_TEST_CONFIG)
	$(HOST_CC) $(MUSASHI_CPPFLAGS) $(MUSASHI_CONFIG_DEFINE) \
		$(MUSASHI_CFLAGS) -c $(MUSASHI_DEP_DIR)/softfloat/softfloat.c \
		-o $@

$(MIGA68K_TEST_PROGRAM): $(MIGA68K_TEST_OBJECTS)
	$(HOST_CC) $(MIGA68K_TEST_OBJECTS) -lm -o $@

$(MIGA68K_TEST_FIXTURE_OBJECT): $(MIGA68K_TEST_FIXTURE_SOURCE)
	@mkdir -p $(MIGA68K_TEST_BUILD_DIR)
	$(TARGET_AS) -m68020 $< -o $@

$(MIGA68K_TEST_FIXTURE_BINARY): $(MIGA68K_TEST_FIXTURE_OBJECT)
	$(TARGET_OBJCOPY) -O binary -j .text $< $@

miga80c: $(MIGA80C_PROGRAM)

$(MIGA80C_PROGRAM): $(MIGA80C_SOURCE) $(COMPILER_SOURCES) \
		$(COMPILER_HEADERS) Makefile
	@mkdir -p $(MIGA80C_BUILD_DIR)
	$(HOST_CC) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) $(COMPILER_SOURCES) \
		$(MIGA80C_SOURCE) -o $@

compiler-amiga-test: $(MIGA80C_AMIGA_PROGRAM) $(MIGA80C_PROGRAM) \
		$(MIGA80C_AMIGA_SPILL_TEST) $(COMPILER_TEST_PROGRAM) \
		$(MIGA80C_AMIGA_ENCODER_TEST) $(COMPILER_ENCODER_TEST_PROGRAM) \
		$(COMPILER_PIPELINE_SOURCE) $(COMPILER_LOCALS_PIPELINE_SOURCE) \
		$(COMPILER_CONDITIONALS_PIPELINE_SOURCE) \
		$(COMPILER_LOOPS_PIPELINE_SOURCE) \
		$(COMPILER_LOOP_CONTROL_PIPELINE_SOURCE) \
		$(COMPILER_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_NARROW_PIPELINE_SOURCE) \
		$(COMPILER_IMMUTABLE_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_CONVERSIONS_PIPELINE_SOURCE) \
		$(MIGA80C_AMIGA_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(MIGA80C_AMIGA_PROGRAM) | \
		tee $(MIGA80C_AMIGA_SIZE_REPORT)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_PIPELINE_SOURCE) \
		--eval 7 5 2 >$(MIGA80C_AMIGA_REPORT)
	diff -u $(MIGA80C_AMIGA_EXPECTED) $(MIGA80C_AMIGA_REPORT)
	$(COMPILER_ENCODER_TEST_PROGRAM) --o1 $(SOURCE_VIEW_FIXTURE) \
		$(MIGA80C_AMIGA_HOST_DIRECT_BINARY) \
		>$(MIGA80C_AMIGA_HOST_DIRECT_REPORT)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_ENCODER_TEST) --o1 $(SOURCE_VIEW_FIXTURE) \
		$(MIGA80C_AMIGA_TARGET_DIRECT_BINARY) \
		>$(MIGA80C_AMIGA_TARGET_DIRECT_REPORT)
	cmp $(MIGA80C_AMIGA_HOST_DIRECT_REPORT) \
		$(MIGA80C_AMIGA_TARGET_DIRECT_REPORT)
	cmp $(MIGA80C_AMIGA_HOST_DIRECT_BINARY) \
		$(MIGA80C_AMIGA_TARGET_DIRECT_BINARY)
	$(MIGA80C_PROGRAM) $(COMPILER_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_TARGET_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_ASSEMBLY) $(MIGA80C_AMIGA_TARGET_ASSEMBLY)
	$(COMPILER_TEST_PROGRAM) --emit-spill-fixture \
		>$(MIGA80C_AMIGA_HOST_SPILL_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_SPILL_TEST) --emit-spill-fixture \
		>$(MIGA80C_AMIGA_TARGET_SPILL_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_SPILL_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_SPILL_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_LOCALS_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_LOCALS_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_LOCALS_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_TARGET_LOCALS_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_LOCALS_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_LOCALS_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_CONDITIONALS_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_CONDITIONALS_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_CONDITIONALS_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_CONDITIONALS_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_CONDITIONALS_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_CONDITIONALS_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_LOOPS_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_LOOPS_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_LOOPS_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_LOOPS_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_LOOPS_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_LOOPS_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_LOOP_CONTROL_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_LOOP_CONTROL_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_LOOP_CONTROL_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_LOOP_CONTROL_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_LOOP_CONTROL_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_LOOP_CONTROL_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_DIVISION_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_DIVISION_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_DIVISION_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_DIVISION_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_DIVISION_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_DIVISION_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_NARROW_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_NARROW_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_NARROW_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_NARROW_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_NARROW_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_NARROW_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_IMMUTABLE_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_IMMUTABLE_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_IMMUTABLE_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_IMMUTABLE_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_IMMUTABLE_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_IMMUTABLE_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_FIXED_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_FIXED_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_FIXED_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_FIXED_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_FIXED_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_FIXED_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_FIXED_PIPELINE_SOURCE) --eval \
		-0.1 0.1 true >$(MIGA80C_AMIGA_HOST_FIXED_EVAL)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_FIXED_PIPELINE_SOURCE) \
		--eval -0.1 0.1 true >$(MIGA80C_AMIGA_TARGET_FIXED_EVAL)
	cmp $(MIGA80C_AMIGA_HOST_FIXED_EVAL) \
		$(MIGA80C_AMIGA_TARGET_FIXED_EVAL)
	$(MIGA80C_PROGRAM) $(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_FIXED_DIVISION_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_FIXED_DIVISION_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_FIXED_DIVISION_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_FIXED_DIVISION_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) --eval \
		-1.0 3.0 1.0 >$(MIGA80C_AMIGA_HOST_FIXED_DIVISION_EVAL)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) \
		--eval -1.0 3.0 1.0 \
		>$(MIGA80C_AMIGA_TARGET_FIXED_DIVISION_EVAL)
	cmp $(MIGA80C_AMIGA_HOST_FIXED_DIVISION_EVAL) \
		$(MIGA80C_AMIGA_TARGET_FIXED_DIVISION_EVAL)
	$(MIGA80C_PROGRAM) $(COMPILER_CONVERSIONS_PIPELINE_SOURCE) -O1 -S \
		-o $(MIGA80C_AMIGA_HOST_CONVERSIONS_ASSEMBLY)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_CONVERSIONS_PIPELINE_SOURCE) \
		-O1 -S -o $(MIGA80C_AMIGA_TARGET_CONVERSIONS_ASSEMBLY)
	cmp $(MIGA80C_AMIGA_HOST_CONVERSIONS_ASSEMBLY) \
		$(MIGA80C_AMIGA_TARGET_CONVERSIONS_ASSEMBLY)
	$(MIGA80C_PROGRAM) $(COMPILER_CONVERSIONS_PIPELINE_SOURCE) --eval \
		-2 -1.75 true >$(MIGA80C_AMIGA_HOST_CONVERSIONS_EVAL)
	PATH="$(MIGA80_PIPX_BIN):$$PATH" vamos -C 20 -- \
		$(MIGA80C_AMIGA_PROGRAM) $(COMPILER_CONVERSIONS_PIPELINE_SOURCE) \
		--eval -2 -1.75 true \
		>$(MIGA80C_AMIGA_TARGET_CONVERSIONS_EVAL)
	cmp $(MIGA80C_AMIGA_HOST_CONVERSIONS_EVAL) \
		$(MIGA80C_AMIGA_TARGET_CONVERSIONS_EVAL)

$(MIGA80C_AMIGA_PROGRAM): $(MIGA80C_SOURCE) $(COMPILER_SOURCES) \
		$(COMPILER_HEADERS) Makefile
	@mkdir -p $(MIGA80C_AMIGA_BUILD_DIR)
	$(TARGET_CC) $(COMPILER_CPPFLAGS) $(TARGET_CFLAGS) $(COMPILER_SOURCES) \
		$(MIGA80C_SOURCE) -Wl,-Map,$(MIGA80C_AMIGA_MAP) -o $@ \
		$(TARGET_RUNTIME)

$(MIGA80C_AMIGA_SPILL_TEST): $(COMPILER_TEST_SOURCE) $(COMPILER_SOURCES) \
		$(COMPILER_HEADERS) Makefile
	@mkdir -p $(MIGA80C_AMIGA_BUILD_DIR)
	$(TARGET_CC) $(COMPILER_CPPFLAGS) $(TARGET_CFLAGS) $(COMPILER_SOURCES) \
		$(COMPILER_TEST_SOURCE) -o $@ $(TARGET_RUNTIME)

$(MIGA80C_AMIGA_ENCODER_TEST): $(COMPILER_ENCODER_TEST_SOURCE) \
		$(COMPILER_SOURCES) $(COMPILER_HEADERS) Makefile
	@mkdir -p $(MIGA80C_AMIGA_BUILD_DIR)
	$(TARGET_CC) $(COMPILER_CPPFLAGS) $(TARGET_CFLAGS) $(COMPILER_SOURCES) \
		$(COMPILER_ENCODER_TEST_SOURCE) -o $@ $(TARGET_RUNTIME)

compiler-abi-test: $(COMPILER_ABI_TEST_PROGRAM) $(COMPILER_ABI_TEST_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(COMPILER_ABI_TEST_PROGRAM) >$(COMPILER_ABI_TEST_REPORT)
	diff -u $(COMPILER_ABI_TEST_EXPECTED) $(COMPILER_ABI_TEST_REPORT)

$(COMPILER_ABI_TEST_PROGRAM): $(COMPILER_ABI_TEST_SOURCE) \
		$(COMPILER_ABI_SOURCE) $(COMPILER_ABI_HEADER) Makefile
	@mkdir -p $(COMPILER_ABI_TEST_BUILD_DIR)
	$(HOST_CC) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) \
		$(COMPILER_ABI_SOURCE) $(COMPILER_ABI_TEST_SOURCE) -o $@

compiler-test: $(COMPILER_TEST_PROGRAM) $(COMPILER_TEST_EXPECTED)
	@mkdir -p $(REPORT_DIR)
	$(COMPILER_TEST_PROGRAM) >$(COMPILER_TEST_REPORT)
	diff -u $(COMPILER_TEST_EXPECTED) $(COMPILER_TEST_REPORT)

$(COMPILER_TEST_PROGRAM): $(COMPILER_TEST_SOURCE) $(COMPILER_SOURCES) \
		$(COMPILER_HEADERS) Makefile
	@mkdir -p $(COMPILER_TEST_BUILD_DIR)
	$(HOST_CC) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) $(COMPILER_SOURCES) \
		$(COMPILER_TEST_SOURCE) -o $@

compiler-execute-test: $(MIGA80C_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(COMPILER_PIPELINE_SOURCE) $(COMPILER_PIPELINE_EXPECTED) \
		$(COMPILER_VALUE_PIPELINE_SOURCE) $(COMPILER_VALUE_PIPELINE_EXPECTED) \
		$(COMPILER_REGISTER_PIPELINE_SOURCE) \
		$(COMPILER_REGISTER_PIPELINE_EXPECTED) \
		$(COMPILER_LOCALS_PIPELINE_SOURCE) \
		$(COMPILER_LOCALS_PIPELINE_EXPECTED) \
		$(COMPILER_CONDITIONALS_PIPELINE_SOURCE) \
		$(COMPILER_CONDITIONALS_PIPELINE_EXPECTED) \
		$(COMPILER_LOOPS_PIPELINE_SOURCE) \
		$(COMPILER_LOOPS_PIPELINE_EXPECTED) \
		$(COMPILER_LOOP_CONTROL_PIPELINE_SOURCE) \
		$(COMPILER_LOOP_CONTROL_PIPELINE_EXPECTED) \
		$(COMPILER_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_DIVISION_PIPELINE_EXPECTED) \
		$(COMPILER_NARROW_PIPELINE_SOURCE) \
		$(COMPILER_NARROW_PIPELINE_EXPECTED) \
		$(COMPILER_IMMUTABLE_PIPELINE_SOURCE) \
		$(COMPILER_IMMUTABLE_PIPELINE_EXPECTED) \
		$(COMPILER_FIXED_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_PIPELINE_EXPECTED) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_EXPECTED) \
		$(COMPILER_CONVERSIONS_PIPELINE_SOURCE) \
		$(COMPILER_CONVERSIONS_PIPELINE_EXPECTED) \
		$(COMPILER_PIPELINE_SCRIPT) $(COMPILER_DIVISION_PIPELINE_SCRIPT) \
		$(COMPILER_NARROW_PIPELINE_SCRIPT) \
		$(COMPILER_IMMUTABLE_PIPELINE_SCRIPT) \
		$(COMPILER_FIXED_PIPELINE_SCRIPT) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_SCRIPT) \
		$(COMPILER_CONVERSIONS_PIPELINE_SCRIPT)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_PIPELINE_SOURCE) \
		$(COMPILER_PIPELINE_BUILD_DIR) $(TARGET_AS) $(TARGET_OBJCOPY) \
		$(COMPILER_PIPELINE_REPORT) $(COMPILER_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_VALUE_PIPELINE_SOURCE) \
		$(COMPILER_VALUE_PIPELINE_BUILD_DIR) $(TARGET_AS) $(TARGET_OBJCOPY) \
		$(COMPILER_VALUE_PIPELINE_REPORT) $(COMPILER_VALUE_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_REGISTER_PIPELINE_SOURCE) \
		$(COMPILER_REGISTER_PIPELINE_BUILD_DIR) $(TARGET_AS) $(TARGET_OBJCOPY) \
		$(COMPILER_REGISTER_PIPELINE_REPORT) \
		$(COMPILER_REGISTER_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_LOCALS_PIPELINE_SOURCE) \
		$(COMPILER_LOCALS_PIPELINE_BUILD_DIR) $(TARGET_AS) $(TARGET_OBJCOPY) \
		$(COMPILER_LOCALS_PIPELINE_REPORT) \
		$(COMPILER_LOCALS_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_CONDITIONALS_PIPELINE_SOURCE) \
		$(COMPILER_CONDITIONALS_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_CONDITIONALS_PIPELINE_REPORT) \
		$(COMPILER_CONDITIONALS_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_LOOPS_PIPELINE_SOURCE) \
		$(COMPILER_LOOPS_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_LOOPS_PIPELINE_REPORT) \
		$(COMPILER_LOOPS_PIPELINE_EXPECTED)
	$(COMPILER_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_LOOP_CONTROL_PIPELINE_SOURCE) \
		$(COMPILER_LOOP_CONTROL_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_LOOP_CONTROL_PIPELINE_REPORT) \
		$(COMPILER_LOOP_CONTROL_PIPELINE_EXPECTED)
	$(COMPILER_DIVISION_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_DIVISION_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_DIVISION_PIPELINE_REPORT) \
		$(COMPILER_DIVISION_PIPELINE_EXPECTED)
	$(COMPILER_NARROW_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_NARROW_PIPELINE_SOURCE) \
		$(COMPILER_NARROW_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_NARROW_PIPELINE_REPORT) \
		$(COMPILER_NARROW_PIPELINE_EXPECTED)
	$(COMPILER_IMMUTABLE_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_IMMUTABLE_PIPELINE_SOURCE) \
		$(COMPILER_IMMUTABLE_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(TARGET_OBJDUMP) \
		$(COMPILER_IMMUTABLE_PIPELINE_REPORT) \
		$(COMPILER_IMMUTABLE_PIPELINE_EXPECTED)
	$(COMPILER_FIXED_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_FIXED_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_FIXED_PIPELINE_REPORT) \
		$(COMPILER_FIXED_PIPELINE_EXPECTED)
	$(COMPILER_FIXED_DIVISION_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_FIXED_DIVISION_PIPELINE_SOURCE) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_FIXED_DIVISION_PIPELINE_REPORT) \
		$(COMPILER_FIXED_DIVISION_PIPELINE_EXPECTED)
	$(COMPILER_CONVERSIONS_PIPELINE_SCRIPT) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_CONVERSIONS_PIPELINE_SOURCE) \
		$(COMPILER_CONVERSIONS_PIPELINE_BUILD_DIR) $(TARGET_AS) \
		$(TARGET_OBJCOPY) $(COMPILER_CONVERSIONS_PIPELINE_REPORT) \
		$(COMPILER_CONVERSIONS_PIPELINE_EXPECTED)

compiler-spill-test: $(COMPILER_TEST_PROGRAM) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_SPILL_SOURCE) \
		$(COMPILER_SPILL_EXPECTED) $(COMPILER_SPILL_SCRIPT)
	$(COMPILER_SPILL_SCRIPT) $(COMPILER_TEST_PROGRAM) $(MIGA80C_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(COMPILER_SPILL_SOURCE) \
		$(COMPILER_SPILL_BUILD_DIR) $(TARGET_AS) $(TARGET_OBJCOPY) \
		$(COMPILER_SPILL_REPORT) $(COMPILER_SPILL_EXPECTED)

c2p-test: $(C2P_HOST_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(C2P_HOST_TEST_PROGRAM) >$(C2P_HOST_TEST_REPORT)
	diff -u $(C2P_HOST_TEST_EXPECTED) $(C2P_HOST_TEST_REPORT)

$(C2P_HOST_TEST_PROGRAM): $(C2P_HOST_TEST_SOURCE) $(C2P_REFERENCE_SOURCES) \
		$(C2P_REFERENCE_HEADER) Makefile
	@mkdir -p $(C2P_HOST_BUILD_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) \
		$(C2P_REFERENCE_SOURCES) $(C2P_HOST_TEST_SOURCE) -o $@

c2p4-test: $(C2P4_HOST_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(C2P4_HOST_TEST_PROGRAM) >$(C2P4_HOST_TEST_REPORT)
	diff -u $(C2P4_HOST_TEST_EXPECTED) $(C2P4_HOST_TEST_REPORT)

$(C2P4_HOST_TEST_PROGRAM): $(C2P4_HOST_TEST_SOURCE) $(C2P4_SOURCES) \
		$(C2P4_HEADER) $(AGA_REFERENCE_SOURCE) $(AGA_REFERENCE_HEADER) \
		$(GRAPHICS_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_HEADER) Makefile
	@mkdir -p $(C2P4_HOST_BUILD_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) \
		$(C2P4_SOURCES) $(AGA_REFERENCE_SOURCE) \
		$(GRAPHICS_REFERENCE_SOURCE) $(C2P4_HOST_TEST_SOURCE) -o $@

graphics-reference-test: $(GRAPHICS_REFERENCE_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(GRAPHICS_REFERENCE_TEST_PROGRAM) >$(GRAPHICS_REFERENCE_TEST_REPORT)
	diff -u $(GRAPHICS_REFERENCE_TEST_EXPECTED) \
		$(GRAPHICS_REFERENCE_TEST_REPORT)

$(GRAPHICS_REFERENCE_TEST_PROGRAM): $(GRAPHICS_REFERENCE_TEST_SOURCE) \
		$(GRAPHICS_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_HEADER) Makefile
	@mkdir -p $(GRAPHICS_REFERENCE_BUILD_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) \
		$(GRAPHICS_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_TEST_SOURCE) -o $@

aga-reference-test: $(AGA_REFERENCE_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(AGA_REFERENCE_TEST_PROGRAM) >$(AGA_REFERENCE_TEST_REPORT)
	diff -u $(AGA_REFERENCE_TEST_EXPECTED) $(AGA_REFERENCE_TEST_REPORT)

$(AGA_REFERENCE_TEST_PROGRAM): $(AGA_REFERENCE_TEST_SOURCE) \
		$(AGA_REFERENCE_SOURCE) $(AGA_REFERENCE_HEADER) \
		$(GRAPHICS_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_HEADER) \
		$(C2P_REFERENCE_SOURCES) $(C2P_REFERENCE_HEADER) Makefile
	@mkdir -p $(AGA_REFERENCE_BUILD_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) \
		$(AGA_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_SOURCE) \
		$(C2P_REFERENCE_SOURCES) $(AGA_REFERENCE_TEST_SOURCE) -o $@

graphics-report-test: $(GRAPHICS_REPORT_TEST_SOURCE) \
		$(GRAPHICS_REPORT_TEST_EXPECTED) $(GRAPHICS_REPORT_VALIDATOR)
	@mkdir -p $(REPORT_DIR)
	LC_ALL=C LANG=C $(GRAPHICS_REPORT_TEST_SOURCE) \
		>$(GRAPHICS_REPORT_TEST_REPORT)
	diff -u $(GRAPHICS_REPORT_TEST_EXPECTED) \
		$(GRAPHICS_REPORT_TEST_REPORT)

chipram-report-test: $(CHIPRAM_REPORT_TEST_SOURCE) \
		$(CHIPRAM_REPORT_TEST_EXPECTED) $(CHIPRAM_REPORT_VALIDATOR)
	@mkdir -p $(REPORT_DIR)
	LC_ALL=C LANG=C $(CHIPRAM_REPORT_TEST_SOURCE) \
		>$(CHIPRAM_REPORT_TEST_REPORT)
	diff -u $(CHIPRAM_REPORT_TEST_EXPECTED) \
		$(CHIPRAM_REPORT_TEST_REPORT)

exclusive-graphics-report-test: $(EXCLUSIVE_GRAPHICS_REPORT_TEST_SOURCE) \
		$(EXCLUSIVE_GRAPHICS_REPORT_TEST_EXPECTED) \
		$(EXCLUSIVE_GRAPHICS_REPORT_VALIDATOR)
	@mkdir -p $(REPORT_DIR)
	LC_ALL=C LANG=C $(EXCLUSIVE_GRAPHICS_REPORT_TEST_SOURCE) \
		>$(EXCLUSIVE_GRAPHICS_REPORT_TEST_REPORT)
	diff -u $(EXCLUSIVE_GRAPHICS_REPORT_TEST_EXPECTED) \
		$(EXCLUSIVE_GRAPHICS_REPORT_TEST_REPORT)

aga-screen: $(AGA_SCREEN_PROGRAM)

$(AGA_SCREEN_PROGRAM): $(AGA_SCREEN_SOURCE) $(C2P_REFERENCE_SOURCE) \
		$(C2P_REFERENCE_HEADER) Makefile
	@mkdir -p $(AGA_SCREEN_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(TARGET_CFLAGS) \
		$(AGA_SCREEN_SOURCE) $(C2P_REFERENCE_SOURCE) \
		-Wl,-Map,$(AGA_SCREEN_MAP) -o $@ $(TARGET_RUNTIME)

aga-screen-inspect: $(AGA_SCREEN_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(AGA_SCREEN_PROGRAM) | tee $(REPORT_DIR)/aga-screen-size.txt
	$(TARGET_NM) --print-size --size-sort $(AGA_SCREEN_PROGRAM) >$(REPORT_DIR)/aga-screen-symbols.txt
	$(TARGET_OBJDUMP) -dr $(AGA_SCREEN_PROGRAM) >$(REPORT_DIR)/aga-screen-disassembly.txt

aga-screen-smoke: fs-uae-smoke aga-screen-inspect
	./scripts/test-fs-uae-runtime.sh $(AGA_SCREEN_PROGRAM) $(AGA_SCREEN_EXPECTED)

c2p-benchmark: $(C2P_BENCHMARK_PROGRAM)

$(C2P_BENCHMARK_PROGRAM): $(C2P_BENCHMARK_SOURCE) \
		$(C2P_REFERENCE_SOURCES) $(C2P_REFERENCE_HEADER) Makefile
	@mkdir -p $(C2P_BENCHMARK_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(C2P_BENCHMARK_CFLAGS) \
		$(C2P_BENCHMARK_SOURCE) $(C2P_REFERENCE_SOURCES) \
		-Wl,-Map,$(C2P_BENCHMARK_MAP) -o $@ $(TARGET_RUNTIME)

c2p-benchmark-inspect: $(C2P_BENCHMARK_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(C2P_BENCHMARK_PROGRAM) | tee $(REPORT_DIR)/c2p-layouts-size.txt
	$(TARGET_NM) --print-size --size-sort $(C2P_BENCHMARK_PROGRAM) >$(REPORT_DIR)/c2p-layouts-symbols.txt
	$(TARGET_OBJDUMP) -dr $(C2P_BENCHMARK_PROGRAM) >$(REPORT_DIR)/c2p-layouts-disassembly.txt

c2p-benchmark-fs-uae: stage c2p-benchmark-inspect
	./scripts/test-fs-uae-runtime.sh $(C2P_BENCHMARK_PROGRAM) -
	@mkdir -p $(REPORT_DIR)
	cp $(STAGING_DIR)/fs-uae-smoke.out $(C2P_BENCHMARK_REPORT)
	./scripts/validate-c2p-benchmark-report.sh $(C2P_BENCHMARK_REPORT)

c2p4-benchmark: $(C2P4_BENCHMARK_PROGRAM)

$(C2P4_BENCHMARK_PROGRAM): $(C2P4_BENCHMARK_SOURCE) \
		$(C2P4_TARGET_SOURCES) \
		$(C2P4_HEADER) $(AGA_REFERENCE_SOURCE) $(AGA_REFERENCE_HEADER) \
		$(GRAPHICS_REFERENCE_SOURCE) $(GRAPHICS_REFERENCE_HEADER) Makefile
	@mkdir -p $(C2P4_BENCHMARK_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(C2P_BENCHMARK_CFLAGS) \
		$(C2P4_BENCHMARK_SOURCE) $(C2P4_TARGET_SOURCES) \
		$(GRAPHICS_REFERENCE_SOURCE) $(AGA_REFERENCE_SOURCE) \
		-Wl,-Map,$(C2P4_BENCHMARK_MAP) -o $@ $(TARGET_RUNTIME)

c2p4-benchmark-inspect: $(C2P4_BENCHMARK_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(C2P4_BENCHMARK_PROGRAM) | \
		tee $(REPORT_DIR)/c2p4-size.txt
	$(TARGET_NM) --print-size --size-sort $(C2P4_BENCHMARK_PROGRAM) \
		>$(REPORT_DIR)/c2p4-symbols.txt
	$(TARGET_OBJDUMP) -dr $(C2P4_BENCHMARK_PROGRAM) \
		>$(REPORT_DIR)/c2p4-disassembly.txt

c2p4-benchmark-fs-uae: stage c2p4-benchmark-inspect
	MIGA80_FS_UAE_TIMEOUT_SECONDS=360 \
		./scripts/test-fs-uae-runtime.sh $(C2P4_BENCHMARK_PROGRAM) -
	@mkdir -p $(REPORT_DIR)
	cp $(STAGING_DIR)/fs-uae-smoke.out $(C2P4_BENCHMARK_REPORT)
	$(GRAPHICS_REPORT_VALIDATOR) $(C2P4_BENCHMARK_REPORT)

chipram-benchmark: $(CHIPRAM_BENCHMARK_PROGRAM)

$(CHIPRAM_BENCHMARK_PROGRAM): $(CHIPRAM_BENCHMARK_SOURCE) \
		$(CHIPRAM_BENCHMARK_ASM_SOURCE) $(CHIPRAM_BENCHMARK_HEADER) \
		Makefile
	@mkdir -p $(CHIPRAM_BENCHMARK_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(C2P_BENCHMARK_CFLAGS) \
		$(CHIPRAM_BENCHMARK_SOURCE) $(CHIPRAM_BENCHMARK_ASM_SOURCE) \
		-Wl,-Map,$(CHIPRAM_BENCHMARK_MAP) -o $@ $(TARGET_RUNTIME)

chipram-benchmark-inspect: $(CHIPRAM_BENCHMARK_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(CHIPRAM_BENCHMARK_PROGRAM) | \
		tee $(REPORT_DIR)/chipram-size.txt
	$(TARGET_NM) --print-size --size-sort $(CHIPRAM_BENCHMARK_PROGRAM) \
		>$(REPORT_DIR)/chipram-symbols.txt
	$(TARGET_OBJDUMP) -dr $(CHIPRAM_BENCHMARK_PROGRAM) \
		>$(REPORT_DIR)/chipram-disassembly.txt

chipram-benchmark-fs-uae: stage chipram-benchmark-inspect
	MIGA80_FS_UAE_TIMEOUT_SECONDS=60 \
		./scripts/test-fs-uae-runtime.sh $(CHIPRAM_BENCHMARK_PROGRAM) -
	@mkdir -p $(REPORT_DIR)
	cp $(STAGING_DIR)/fs-uae-smoke.out $(CHIPRAM_BENCHMARK_REPORT)
	$(CHIPRAM_REPORT_VALIDATOR) $(CHIPRAM_BENCHMARK_REPORT)

exclusive-graphics-benchmark: $(EXCLUSIVE_GRAPHICS_PROGRAM)

$(EXCLUSIVE_GRAPHICS_PROGRAM): $(EXCLUSIVE_GRAPHICS_SOURCE) \
		$(CHIPRAM_BENCHMARK_ASM_SOURCE) $(CHIPRAM_BENCHMARK_HEADER) \
		$(C2P4_TARGET_SOURCES) $(C2P4_HEADER) Makefile
	@mkdir -p $(EXCLUSIVE_GRAPHICS_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(C2P_BENCHMARK_CFLAGS) \
		$(EXCLUSIVE_GRAPHICS_SOURCE) $(CHIPRAM_BENCHMARK_ASM_SOURCE) \
		$(C2P4_TARGET_SOURCES) \
		-Wl,-Map,$(EXCLUSIVE_GRAPHICS_MAP) -o $@ $(TARGET_RUNTIME)

exclusive-graphics-benchmark-inspect: $(EXCLUSIVE_GRAPHICS_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(TARGET_SIZE) $(EXCLUSIVE_GRAPHICS_PROGRAM) | \
		tee $(REPORT_DIR)/exclusive-graphics-size.txt
	$(TARGET_NM) --print-size --size-sort $(EXCLUSIVE_GRAPHICS_PROGRAM) \
		>$(REPORT_DIR)/exclusive-graphics-symbols.txt
	$(TARGET_OBJDUMP) -dr $(EXCLUSIVE_GRAPHICS_PROGRAM) \
		>$(REPORT_DIR)/exclusive-graphics-disassembly.txt

exclusive-graphics-benchmark-fs-uae: stage \
		exclusive-graphics-benchmark-inspect
	MIGA80_FS_UAE_TIMEOUT_SECONDS=600 \
		./scripts/test-fs-uae-runtime.sh $(EXCLUSIVE_GRAPHICS_PROGRAM) -
	@mkdir -p $(REPORT_DIR)
	cp $(STAGING_DIR)/fs-uae-smoke.out $(EXCLUSIVE_GRAPHICS_REPORT)
	$(EXCLUSIVE_GRAPHICS_REPORT_VALIDATOR) $(EXCLUSIVE_GRAPHICS_REPORT)

exclusive-graphics-benchmark-fs-uae-fast: stage \
		exclusive-graphics-benchmark-inspect
	MIGA80_FS_UAE_TIMEOUT_SECONDS=600 \
	MIGA80_FS_UAE_FAST_MEMORY_KIB=2048 \
		./scripts/test-fs-uae-runtime.sh $(EXCLUSIVE_GRAPHICS_PROGRAM) -
	@mkdir -p $(REPORT_DIR)
	cp $(STAGING_DIR)/fs-uae-smoke.out $(EXCLUSIVE_GRAPHICS_FAST_REPORT)
	$(EXCLUSIVE_GRAPHICS_REPORT_VALIDATOR) $(EXCLUSIVE_GRAPHICS_FAST_REPORT)

$(EXCLUSIVE_GRAPHICS_PHYSICAL_PROGRAM): $(EXCLUSIVE_GRAPHICS_SOURCE) \
		$(CHIPRAM_BENCHMARK_ASM_SOURCE) $(CHIPRAM_BENCHMARK_HEADER) \
		$(C2P4_TARGET_SOURCES) $(C2P4_HEADER) Makefile
	@mkdir -p $(EXCLUSIVE_GRAPHICS_PHYSICAL_BUILD_DIR)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(C2P_BENCHMARK_CFLAGS) \
		-DMIGA80_BENCHMARK_ENVIRONMENT=\"physical_a1200_pal_candidate\" \
		-DMIGA80_BENCHMARK_AUTHORITY=\"real_hardware_candidate\" \
		-DMIGA80_BENCHMARK_REPORT_PATH=\"MIGA80BENCH:RESULT.TXT\" \
		$(EXCLUSIVE_GRAPHICS_SOURCE) $(CHIPRAM_BENCHMARK_ASM_SOURCE) \
		$(C2P4_TARGET_SOURCES) \
		-Wl,-Map,$(EXCLUSIVE_GRAPHICS_PHYSICAL_MAP) -o $@ \
		$(TARGET_RUNTIME)

exclusive-graphics-test-adf: $(EXCLUSIVE_GRAPHICS_TEST_ADF)

$(EXCLUSIVE_GRAPHICS_TEST_ADF): $(EXCLUSIVE_GRAPHICS_PHYSICAL_PROGRAM) \
		$(EXCLUSIVE_GRAPHICS_PHYSICAL_STARTUP) \
		$(EXCLUSIVE_GRAPHICS_PHYSICAL_README) \
		$(EXCLUSIVE_GRAPHICS_ADF_BUILDER)
	$(EXCLUSIVE_GRAPHICS_ADF_BUILDER) \
		$(EXCLUSIVE_GRAPHICS_PHYSICAL_PROGRAM) \
		$(EXCLUSIVE_GRAPHICS_PHYSICAL_STARTUP) \
		$(EXCLUSIVE_GRAPHICS_PHYSICAL_README) $@

exclusive-graphics-test-adf-inspect: $(EXCLUSIVE_GRAPHICS_TEST_ADF)
	xdfscan $(EXCLUSIVE_GRAPHICS_TEST_ADF)
	xdftool $(EXCLUSIVE_GRAPHICS_TEST_ADF) list
	@printf 'Manifest: %s\n' $(EXCLUSIVE_GRAPHICS_TEST_ADF_MANIFEST)

exclusive-graphics-test-adf-fs-uae: $(EXCLUSIVE_GRAPHICS_TEST_ADF) \
		$(EXCLUSIVE_GRAPHICS_ADF_TESTER) \
		$(EXCLUSIVE_GRAPHICS_REPORT_VALIDATOR)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=600 \
		$(EXCLUSIVE_GRAPHICS_ADF_TESTER) $(EXCLUSIVE_GRAPHICS_TEST_ADF)

runtime-compare:
	./scripts/compare-c-runtimes.sh

check: solid-cube-test triangle-asm-test intro-test c2p4-asm-test animation-test drawing-test runtime-guards-test miga68k-test compiler-abi-test compiler-test compiler-execute-test \
	compiler-spill-test compiler-amiga-test c2p-test \
	c2p4-test graphics-reference-test aga-reference-test \
	graphics-report-test chipram-report-test exclusive-graphics-report-test \
	source-view-test editor-test compiler-encoder-test compiler-encoder-musashi-test \
	compiler-call-test \
	chipram-benchmark exclusive-graphics-benchmark inspect vamos-test \
	aga-screen-smoke

clean:
	rm -rf $(AMIGA_BUILD_DIR) $(HOST_BUILD_DIR) $(MUSASHI_DEP_DIR) \
		$(REPORT_DIR) \
		$(SMOKE_BUILD_DIR) $(BENCHMARK_BUILD_DIR) $(DISTRIBUTION_DIR) \
		build/fs-uae-smoke build/fs-uae-physical-adf \
		build/fs-uae-demo-adf build/generated build/runtime-comparison
	rm -f $(STAGED_PROGRAM) $(STAGING_DIR)/fs-uae-smoke.out

ANIMATION_TEST_PROGRAM := $(HOST_BUILD_DIR)/animation/test
$(ANIMATION_TEST_PROGRAM): tests/host/animation/main.c src/graphics/drawing.c src/graphics/triangle.c \
        src/graphics/drawing.h $(COMPILER_SOURCES) $(COMPILER_HEADERS) \
        $(COMPILER_ENCODER_SOURCE) $(COMPILER_ENCODER_HEADER) compiler/abi/runtime.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) \
		-fsanitize=address,undefined tests/host/animation/main.c src/graphics/drawing.c src/graphics/triangle.c \
		$(COMPILER_SOURCES) -lm -o $@

.PHONY: animation-test
animation-test: $(ANIMATION_TEST_PROGRAM) $(COMPILER_ENCODER_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-animation.py $(ANIMATION_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) >$(REPORT_DIR)/animation-host.txt
	$(PYTHON) scripts/test-animation-math.py $(COMPILER_ENCODER_TEST_PROGRAM) \
		$(MIGA68K_TEST_PROGRAM) $(TARGET_AS) $(TARGET_OBJCOPY) >$(REPORT_DIR)/animation-math.txt
	@cat $(REPORT_DIR)/animation-host.txt $(REPORT_DIR)/animation-math.txt

.PHONY: miga80-cube-adf miga80-cube-fs-uae
miga80-cube-adf: build/distribution/miga80-cube.adf
build/distribution/miga80-cube.adf: $(MIGA80_DEMO_PROGRAM) assets/demo/cube.lua \
        assets/demo/layers.lua assets/demo/Startup-Cube $(MIGA80_DEMO_README) \
        $(FONT4X8_GENERATED_BINARY) LICENSE $(MIGA80_DEMO_ADF_BUILDER)
	$(MIGA80_DEMO_ADF_BUILDER) $(MIGA80_DEMO_PROGRAM) assets/demo/cube.lua \
		$(FONT4X8_GENERATED_BINARY) assets/demo/Startup-Cube $(MIGA80_DEMO_README) \
		LICENSE $@ assets/demo/layers.lua assets/demo/cube.lua

miga80-cube-fs-uae: build/distribution/miga80-cube.adf
	MIGA80_FS_UAE_TIMEOUT_SECONDS=240 $(MIGA80_DEMO_ADF_TESTER) $< \
		tests/smoke/source-view-adf/cube-expected.txt CUBETEST

.PHONY: animation-chunky-test miga80-cube-chunky-adf miga80-cube-chunky-fs-uae
animation-chunky-test: $(ANIMATION_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-animation.py $(ANIMATION_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) assets/demo/cube-chunky.lua \
		$(HOST_BUILD_DIR)/animation-chunky PIXEL >$(REPORT_DIR)/animation-chunky-host.txt
	@cat $(REPORT_DIR)/animation-chunky-host.txt

miga80-cube-chunky-adf: build/distribution/miga80-cube-chunky.adf
build/distribution/miga80-cube-chunky.adf: $(MIGA80_DEMO_PROGRAM) assets/demo/cube-chunky.lua \
        assets/demo/layers.lua assets/demo/Startup-Cube $(MIGA80_DEMO_README) \
        $(FONT4X8_GENERATED_BINARY) LICENSE $(MIGA80_DEMO_ADF_BUILDER)
	$(MIGA80_DEMO_ADF_BUILDER) $(MIGA80_DEMO_PROGRAM) assets/demo/cube-chunky.lua \
		$(FONT4X8_GENERATED_BINARY) assets/demo/Startup-Cube $(MIGA80_DEMO_README) \
		LICENSE $@ assets/demo/layers.lua assets/demo/cube-chunky.lua

miga80-cube-chunky-fs-uae: build/distribution/miga80-cube-chunky.adf
	MIGA80_FS_UAE_TIMEOUT_SECONDS=360 $(MIGA80_DEMO_ADF_TESTER) $< \
		tests/smoke/source-view-adf/cube-chunky-expected.txt CUBEPIXELTEST

.PHONY: c2p4-asm-test miga80-cube-c2p-compare
c2p4-asm-test: $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(HOST_BUILD_DIR)/c2p4-asm $(REPORT_DIR)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_CFLAGS) -fsanitize=address,undefined \
		tests/host/c2p4-kalms/main.c src/graphics/c2p4_kalms.c $(C2P4_REFERENCE_SOURCE) \
		-o $(HOST_BUILD_DIR)/c2p4-asm/wrapper-test
	$(HOST_BUILD_DIR)/c2p4-asm/wrapper-test >$(REPORT_DIR)/c2p4-asm.txt
	$(PYTHON) scripts/test-c2p4-asm.py $(MIGA68K_TEST_PROGRAM) $(TARGET_CC) $(TARGET_OBJCOPY) \
		>>$(REPORT_DIR)/c2p4-asm.txt
	@tail -n 1 $(REPORT_DIR)/c2p4-asm.txt

# The ADF harness has one shared run directory; these runs must stay sequential.
miga80-cube-c2p-compare: c2p4-asm-test build/distribution/miga80-cube-chunky.adf
	cp build/distribution/miga80-cube-chunky.adf build/distribution/miga80-cube-c2p-tested.adf
	MIGA80_C2P_BACKEND=REFERENCE $(MAKE) miga80-cube-chunky-fs-uae
	MIGA80_C2P_BACKEND=MASK32 $(MAKE) miga80-cube-chunky-fs-uae
	MIGA80_C2P_BACKEND=KALMS $(MAKE) miga80-cube-chunky-fs-uae
	$(PYTHON) scripts/report-cube-c2p.py

.PHONY: miga80-cube-chunky-kalms-adf
miga80-cube-chunky-kalms-adf: build/distribution/miga80-cube-chunky-kalms.adf
build/distribution/miga80-cube-chunky-kalms.adf: $(MIGA80_DEMO_PROGRAM) assets/demo/cube-chunky.lua \
        assets/demo/layers.lua assets/demo/Startup-Cube-Kalms $(MIGA80_DEMO_README) \
        $(FONT4X8_GENERATED_BINARY) LICENSE $(MIGA80_DEMO_ADF_BUILDER)
	$(MIGA80_DEMO_ADF_BUILDER) $(MIGA80_DEMO_PROGRAM) assets/demo/cube-chunky.lua \
		$(FONT4X8_GENERATED_BINARY) assets/demo/Startup-Cube-Kalms $(MIGA80_DEMO_README) \
		LICENSE $@ assets/demo/layers.lua assets/demo/cube-chunky.lua

# Reference distribution: all demo assets, a SYS: browser, no benchmark fixtures.
MIGA80_RELEASE_DEMOS := $(sort $(wildcard assets/demo/*.lua))
MIGA80_RELEASE_BASENAME := miga80-$(MIGA80_VERSION)
MIGA80_RELEASE_ADF := release/$(MIGA80_RELEASE_BASENAME).adf
MIGA80_RELEASE_MANIFEST := release/$(MIGA80_RELEASE_BASENAME).manifest.json
.PHONY: release release-fs-uae release-boot-fs-uae
release: $(MIGA80_RELEASE_ADF) $(MIGA80_RELEASE_MANIFEST)
$(MIGA80_RELEASE_ADF) $(MIGA80_RELEASE_MANIFEST) &: $(MIGA80_DEMO_PROGRAM) $(MIGA80_RELEASE_DEMOS) assets/demo \
        $(FONT4X8_GENERATED_BINARY) assets/demo/Startup-Browser $(MIGA80_DEMO_README) \
        LICENSE third_party/kalms-c2p/readme.txt third_party/ptplayer/LICENSE works/mods/93_10_12_A_SYNTH_1.mod scripts/build-miga80-release.py $(MIGA80_VERSION_FILE)
	$(PYTHON) scripts/build-miga80-release.py $(MIGA80_DEMO_PROGRAM) \
		$(FONT4X8_GENERATED_BINARY) $(MIGA80_RELEASE_ADF) $(MIGA80_VERSION)

release-fs-uae: $(MIGA80_RELEASE_ADF) $(MIGA80_RELEASE_MANIFEST)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=240 $(MIGA80_DEMO_ADF_TESTER) $< \
		tests/smoke/source-view-adf/browser-expected.txt BROWSERTEST

release-boot-fs-uae: $(MIGA80_RELEASE_ADF) $(MIGA80_RELEASE_MANIFEST)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=60 $(MIGA80_DEMO_ADF_TESTER) $< \
		tests/smoke/source-view-adf/browse-ready-expected.txt BROWSE

$(BOOT_LOGO_HEADER): works/logo.png scripts/generate-boot-logo.py
	$(PYTHON) scripts/generate-boot-logo.py $< $@

$(BOOT_JINGLE_OBJECT): src/audio/boot_jingle.c src/audio/boot_jingle.h Makefile
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(PROJECT_CPPFLAGS) $(TARGET_CFLAGS) -m68000 -c $< -o $@

.PHONY: release-intro-fs-uae
release-intro-fs-uae: $(MIGA80_RELEASE_ADF) $(MIGA80_RELEASE_MANIFEST)
	MIGA80_FS_UAE_TIMEOUT_SECONDS=120 $(MIGA80_DEMO_ADF_TESTER) $< \
		tests/smoke/source-view-adf/intro-expected.txt INTROTEST

INTRO_HOST_PROGRAM := $(HOST_BUILD_DIR)/intro/test
.PHONY: intro-test
$(INTRO_HOST_PROGRAM): tests/host/intro/main.c src/demo/intro_effect.c \
        src/demo/intro_effect.h src/audio/boot_jingle.c src/audio/boot_jingle.h $(BOOT_LOGO_HEADER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) -Ibuild/generated $(HOST_CFLAGS) \
		-fsanitize=address,undefined tests/host/intro/main.c src/demo/intro_effect.c \
		src/audio/boot_jingle.c -o $@

intro-test: $(INTRO_HOST_PROGRAM)
	@mkdir -p $(REPORT_DIR)/intro
	$(INTRO_HOST_PROGRAM) $(REPORT_DIR)/intro >$(REPORT_DIR)/intro-host.txt
	$(PYTHON) scripts/preview-boot-intro.py $(REPORT_DIR)/intro >>$(REPORT_DIR)/intro-host.txt
	@cat $(REPORT_DIR)/intro-host.txt

SOLID_CUBE_TEST_PROGRAM := $(HOST_BUILD_DIR)/solid-cube/test
$(SOLID_CUBE_TEST_PROGRAM): tests/host/solid-cube/main.c src/graphics/drawing.c src/graphics/triangle.c \
        src/graphics/drawing.h $(COMPILER_SOURCES) $(COMPILER_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) \
		-fsanitize=address,undefined $< src/graphics/drawing.c src/graphics/triangle.c \
		$(COMPILER_SOURCES) -lm -o $@

.PHONY: solid-cube-test
solid-cube-test: $(SOLID_CUBE_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-solid-cube.py $(SOLID_CUBE_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) assets/demo/cube-solid.lua \
		$(HOST_BUILD_DIR)/solid-cube PLANAR >$(REPORT_DIR)/solid-cube-host.txt
	$(PYTHON) scripts/test-solid-cube.py $(SOLID_CUBE_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) assets/demo/cube-solid-chunky.lua \
		$(HOST_BUILD_DIR)/solid-cube-chunky PIXEL >$(REPORT_DIR)/solid-cube-chunky-host.txt
	cmp $(HOST_BUILD_DIR)/solid-cube/frames.bin $(HOST_BUILD_DIR)/solid-cube-chunky/frames.bin
	@cat $(REPORT_DIR)/solid-cube-host.txt $(REPORT_DIR)/solid-cube-chunky-host.txt

.PHONY: solid-cube-fs-uae solid-cube-chunky-fs-uae
solid-cube-fs-uae: release
	MIGA80_FS_UAE_TIMEOUT_SECONDS=240 $(MIGA80_DEMO_ADF_TESTER) $(MIGA80_RELEASE_ADF) \
		tests/smoke/source-view-adf/solid-expected.txt SOLIDTEST
solid-cube-chunky-fs-uae: release
	MIGA80_FS_UAE_TIMEOUT_SECONDS=360 $(MIGA80_DEMO_ADF_TESTER) $(MIGA80_RELEASE_ADF) \
		tests/smoke/source-view-adf/cube-chunky-expected.txt SOLIDPIXELTEST

.PHONY: triangle-asm-test
triangle-asm-test: $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-triangle-asm.py $(MIGA68K_TEST_PROGRAM) $(TARGET_CC) $(TARGET_OBJCOPY) >$(REPORT_DIR)/triangle-asm.txt
	@cat $(REPORT_DIR)/triangle-asm.txt

MUSIC_TEST_PROGRAM := $(HOST_BUILD_DIR)/music/test
$(MUSIC_TEST_PROGRAM): tests/host/music/main.c src/audio/mod.c src/audio/mod.h $(COMPILER_SOURCES) $(COMPILER_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(COMPILER_CPPFLAGS) $(HOST_CFLAGS) -fsanitize=address,undefined \
		tests/host/music/main.c src/audio/mod.c $(COMPILER_SOURCES) -o $@
.PHONY: music-test
music-test: $(MUSIC_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM)
	@mkdir -p $(REPORT_DIR)
	$(PYTHON) scripts/test-music.py $(MUSIC_TEST_PROGRAM) $(MIGA68K_TEST_PROGRAM) \
		$(TARGET_CC) $(TARGET_AS) $(TARGET_OBJCOPY) >$(REPORT_DIR)/music-host.txt
	@cat $(REPORT_DIR)/music-host.txt
check: music-test
