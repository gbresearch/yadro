#-----------------------------------------------------------------------------
#  Copyright (C) 2011-2026, Gene Bushuyev
#
#  Boost Software License - Version 1.0 - August 17th, 2003
#  (see the LICENSE file)
#-----------------------------------------------------------------------------
#
# GNU Makefile that builds yadro with GCC or Clang on Linux, the counterpart of the Visual Studio
# projects in vs/: the same static library (libyadro.a) from the same sources, and the same test
# executable (yadro_test).
#
#   make -j8                      library and test executable, GCC, release
#   make -j8 TOOLCHAIN=clang      the same with clang++
#   make -j8 CXX=g++-15           a specific compiler (GCC or Clang is detected from its --version)
#   make -j8 CONFIG=debug         debug build (-O0 -g -D_DEBUG) instead of release (-O2 -DNDEBUG)
#   make -j8 WERROR=1             warnings are errors, as in the Visual Studio projects
#   make lib                      only the library
#   make test                     build and run the tests; TEST_ARGS="--run-all" passes arguments
#   make check-headers            each header compiles on its own, as tools/check_headers.ps1 checks
#   make clean                    remove the outputs of this compiler and configuration
#   make clean-all                remove the outputs of every compiler and configuration on this OS
#   make help                     this text
#
# Requirements: -std=c++23 with deducing this, std::expected and std::stacktrace, so GCC 14 or later,
# or Clang 20 or later with libstdc++ 14 or later (the standard library Clang uses on Linux; libc++
# has no <stacktrace>). std::stacktrace needs libstdc++exp, which is linked (STACKTRACE_LIBS).
#
# JSON parsing (json_value, json_db's read_json) needs AXE. When AXE_INCLUDE (default ../axe/include,
# a sibling checkout as for the Visual Studio projects) has axe.h, the build defines
# GB_YADRO_ENABLE_AXE_JSON and adds AXE to the include path; otherwise the library builds without
# it, and the tests that need it are compiled out. AXE=1 or AXE=0 forces it on or off.
#
# CPPFLAGS, CXXFLAGS, LDFLAGS and LDLIBS given on the command line are added to the flags below, e.g.
# make CXXFLAGS=-fsanitize=address LDFLAGS=-fsanitize=address.
#
# Outputs, laid out as in the Visual Studio projects, where PLATFORM is <os>-<compiler>, e.g.
# linux-g++-14, and Config is Release or Debug:
#   lib/<PLATFORM>/<Config>/libyadro.a
#   exe/<PLATFORM>/<Config>/yadro_test           the tests run here and write yadro-test.log
#   obj/<project>/<PLATFORM>/<Config>/...
#-----------------------------------------------------------------------------

.DEFAULT_GOAL := all

TOOLCHAIN ?= gcc
CONFIG    ?= release

ifeq ($(origin CXX),default)
  ifeq ($(TOOLCHAIN),clang)
    CXX := clang++
  else ifeq ($(TOOLCHAIN),gcc)
    CXX := g++
  else
    $(error TOOLCHAIN must be gcc or clang, not '$(TOOLCHAIN)')
  endif
endif

COMPILER_ID    := $(if $(findstring clang,$(shell $(CXX) --version 2>/dev/null)),clang,gcc)
COMPILER_MAJOR := $(firstword $(subst ., ,$(shell $(CXX) -dumpversion 2>/dev/null)))
MIN_MAJOR      := $(if $(filter clang,$(COMPILER_ID)),20,14)

# an older compiler fails with errors deep in the headers, so say what is needed instead
ifneq ($(filter-out help clean clean-all,$(or $(MAKECMDGOALS),all)),)
  ifeq ($(shell [ "$(COMPILER_MAJOR)" -ge $(MIN_MAJOR) ] 2>/dev/null && echo ok),)
    $(error $(CXX) is $(COMPILER_ID) $(or $(COMPILER_MAJOR),of unknown version); yadro needs GCC 14 or Clang 20 or later, e.g. make CXX=g++-14)
  endif
endif

ifeq ($(CONFIG),release)
  CONFIG_DIR   := Release
  CONFIG_FLAGS := -O2 -DNDEBUG
else ifeq ($(CONFIG),debug)
  CONFIG_DIR   := Debug
  CONFIG_FLAGS := -O0 -g -D_DEBUG
else
  $(error CONFIG must be release or debug, not '$(CONFIG)')
endif

OS_NAME  := $(shell uname -s | tr '[:upper:]' '[:lower:]')
PLATFORM ?= $(OS_NAME)-$(notdir $(firstword $(CXX)))

#-----------------------------------------------------------------------------
# flags

# The Visual Studio projects compile with /arch:AVX2, which enables the AVX2 code paths (FFT, xxHash128).
# ARCH_FLAGS= (empty) builds for any x86-64 CPU; ARCH_FLAGS=-march=native for the building machine.
ARCH_FLAGS ?= -mavx2 -mfma

WARN_FLAGS ?= -Wall -Wextra
ifeq ($(WERROR),1)
  WARN_FLAGS += -Werror
endif

# The test sources keep results they do not use (e.g. tree nodes, for readability) and helpers that
# some configurations do not call, so these warnings are off for them, and only for them.
TEST_WARN_FLAGS ?= -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers
ifeq ($(COMPILER_ID),clang)
  TEST_WARN_FLAGS += -Wno-unused-lambda-capture -Wno-unneeded-internal-declaration
else
  # util_test.cpp replaces the global operator new and delete with malloc and free, which GCC reports
  # as mismatched once both are inlined; and GCC 12 to 14 report out-of-bounds copies of a small
  # std::vector that are in bounds (chebyshev_filter on a constant one-value input)
  TEST_WARN_FLAGS += -Wno-mismatched-new-delete -Wno-array-bounds -Wno-stringop-overflow
endif

STACKTRACE_LIBS ?= -lstdc++exp

AXE_INCLUDE ?= ../axe/include
ifeq ($(origin AXE),undefined)
  AXE := $(if $(wildcard $(AXE_INCLUDE)/axe.h),1,0)
endif
ifeq ($(AXE),1)
  AXE_FLAGS := -DGB_YADRO_ENABLE_AXE_JSON -isystem $(AXE_INCLUDE)
endif

CXXSTD        := -std=c++23
YADRO_FLAGS    = $(AXE_FLAGS) $(CPPFLAGS) $(CXXSTD) $(CONFIG_FLAGS) $(ARCH_FLAGS) -pthread
DEPFLAGS       = -MMD -MP

#-----------------------------------------------------------------------------
# sources: the same as in vs/yadro.vcxproj and vs/yadro_test.vcxproj

LIB_SOURCES := \
    algorithm/adf_test.cpp \
    algorithm/mackinnon.cpp \
    container/gbdb.cpp \
    container/tree.cpp \
    simulator/fiber.cpp \
    simulator/scheduler.cpp \
    util/file_mutex.cpp \
    util/string_util.cpp \
    util/win_service.cpp

TEST_SOURCES := \
    test/algorithm_test.cpp \
    test/archive_test.cpp \
    test/async_test.cpp \
    test/container_test.cpp \
    test/durable_file_test.cpp \
    test/graph_test.cpp \
    test/json_test.cpp \
    test/simulator_test.cpp \
    test/util_test.cpp \
    test/yadro_test.cpp

LIB_DIR      := lib/$(PLATFORM)/$(CONFIG_DIR)
EXE_DIR      := exe/$(PLATFORM)/$(CONFIG_DIR)
LIB_OBJ_DIR  := obj/yadro/$(PLATFORM)/$(CONFIG_DIR)
TEST_OBJ_DIR := obj/yadro_test/$(PLATFORM)/$(CONFIG_DIR)
CHECK_DIR    := obj/header_check/$(PLATFORM)

LIBRARY  := $(LIB_DIR)/libyadro.a
TEST_EXE := $(EXE_DIR)/yadro_test

LIB_OBJECTS  := $(LIB_SOURCES:%.cpp=$(LIB_OBJ_DIR)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.cpp=$(TEST_OBJ_DIR)/%.o)

#-----------------------------------------------------------------------------
# build

.PHONY: all lib test_exe test check-headers clean clean-all help

all: lib test_exe

lib: $(LIBRARY)

test_exe: $(TEST_EXE)

$(LIBRARY): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(TEST_EXE): $(TEST_OBJECTS) $(LIBRARY)
	@mkdir -p $(@D)
	$(CXX) -pthread $(LDFLAGS) -o $@ $(TEST_OBJECTS) $(LIBRARY) $(STACKTRACE_LIBS) $(LDLIBS)

$(LIB_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(YADRO_FLAGS) $(WARN_FLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(YADRO_FLAGS) $(WARN_FLAGS) $(TEST_WARN_FLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

test: $(TEST_EXE)
ifneq ($(AXE),1)
	@echo "note: built without AXE ($(AXE_INCLUDE)/axe.h not found), so the JSON parsing tests are reported as DISABLED"
endif
	cd $(EXE_DIR) && ./yadro_test $(TEST_ARGS)

#-----------------------------------------------------------------------------
# check-headers: the counterpart of tools/check_headers.ps1. Compiles, as a syntax and semantic check
# at -Wall -Wextra -Werror, one translation unit per header under util/, container/, archive/ and
# async/ that holds only #include <dir/header.h>, GB_TEST and GB_TEST_IF with and without a policy
# argument, and each library source.

CHECK_HEADERS := $(sort $(wildcard util/*.h container/*.h archive/*.h async/*.h))
CHECK_UNITS   := $(CHECK_HEADERS:%.h=$(CHECK_DIR)/%.h.ok) $(CHECK_DIR)/gb_test_usage.ok \
                 $(LIB_SOURCES:%.cpp=$(CHECK_DIR)/%.cpp.ok)
CHECK_FLAGS    = $(AXE_FLAGS) $(CPPFLAGS) -I. $(CXXSTD) $(ARCH_FLAGS) -Wall -Wextra -Werror $(CXXFLAGS) \
                 -fsyntax-only -MMD -MP -MF $@.d -MT $@

check-headers: $(CHECK_UNITS)
	@echo "check-headers: all $(words $(CHECK_UNITS)) translation units compiled ($(CXX), AXE=$(AXE))"

$(CHECK_DIR)/%.h.ok: %.h
	@mkdir -p $(@D)
	@printf '#include <%s>\n' $< > $(CHECK_DIR)/$*.h.cpp
	$(CXX) $(CHECK_FLAGS) $(CHECK_DIR)/$*.h.cpp
	@touch $@

$(CHECK_DIR)/gb_test_usage.ok: util/gbtest.h
	@mkdir -p $(@D)
	@printf '%s\n' '#include <util/gbtest.h>' \
	    'GB_TEST(header_check, without_policy) {}' 'GB_TEST(header_check, with_policy, std::launch::async) {}' \
	    'GB_TEST_IF(true, header_check, if_without_policy) {}' 'GB_TEST_IF(false, header_check, if_with_policy, std::launch::async) {}' \
	    > $(CHECK_DIR)/gb_test_usage.cpp
	$(CXX) $(CHECK_FLAGS) $(CHECK_DIR)/gb_test_usage.cpp
	@touch $@

$(CHECK_DIR)/%.cpp.ok: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CHECK_FLAGS) $<
	@touch $@

#-----------------------------------------------------------------------------

clean:
	rm -rf $(LIB_DIR) $(EXE_DIR) $(LIB_OBJ_DIR) $(TEST_OBJ_DIR) $(CHECK_DIR)

clean-all:
	rm -rf lib/$(OS_NAME)-* exe/$(OS_NAME)-* obj/*/$(OS_NAME)-*

help:
	@sed -n '/^# GNU Makefile/,/^#----/p' $(firstword $(MAKEFILE_LIST)) | sed -e '$$d' -e 's/^# \{0,1\}//'

-include $(LIB_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(CHECK_UNITS:=.d)
