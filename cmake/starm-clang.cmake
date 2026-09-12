set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID Clang)
set(CMAKE_CXX_COMPILER_ID Clang)

# Some default llvm settings
set(TOOLCHAIN_PREFIX                starm-)

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}clang)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}clang++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}clang)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# STARM_TOOLCHAIN_CONFIG allows you to choose the toolchain configuration.
# Possible values are:
#  "STARM_HYBRID"   : Hybrid configuration using starm-clang Assemler and Compiler and GNU Linker
#  "STARM_NEWLIB"   : starm-clang toolchain with NEWLIB C library
#  "STARM_PICOLIBC" : starm-clang toolchain with PICOLIBC C library
# Note: this bundle's st-arm-clang install ships newlib.cfg but no picolibc.cfg,
# so STARM_PICOLIBC will fail to configure here.
set(STARM_TOOLCHAIN_CONFIG "STARM_NEWLIB")

if(STARM_TOOLCHAIN_CONFIG STREQUAL "STARM_HYBRID")
  set(TOOLCHAIN_MULTILIBS "--multi-lib-config=\"$ENV{CLANG_GCC_CMSIS_COMPILER}/multilib.gnu_tools_for_stm32.yaml\" --gcc-toolchain=\"$ENV{GCC_TOOLCHAIN_ROOT}/..\"")
elseif (STARM_TOOLCHAIN_CONFIG STREQUAL "STARM_NEWLIB")
  # Point --sysroot straight at the pre-resolved multilib variant for this
  # MCU (cortex-m0plus, soft float, no FPU, size-optimized) instead of using
  # --config=newlib.cfg (which sets --sysroot to the multilib *root* and
  # triggers Clang's multilib.yaml-based auto-selection). That auto-selection
  # works fine in starm-clang itself, but the separately-installed
  # clang-scan-deps (see below) uses upstream LLVM's multilib parser, which
  # doesn't understand this bundle's multilib.yaml (fails with "unknown key
  # 'IncludeDirs'") and silently falls back to the wrong variant. Bypassing
  # multilib resolution entirely sidesteps that incompatibility.
  find_program(STARM_CLANG_EXE NAMES ${TOOLCHAIN_PREFIX}clang)
  get_filename_component(STARM_CLANG_BINDIR "${STARM_CLANG_EXE}" DIRECTORY)
  set(STARM_RUNTIME_ROOT "${STARM_CLANG_BINDIR}/../lib/clang-runtimes/newlib")
  set(STARM_RUNTIME_SYSROOT "${STARM_RUNTIME_ROOT}/arm-none-eabi/armv6m_soft_nofp_size")
  set(TOOLCHAIN_MULTILIBS "--sysroot=\"${STARM_RUNTIME_SYSROOT}\"")
  set(TOOLCHAIN_LINK_FLAGS "-Wl,--no-auto-printf-variant-selection -Wl,--no-auto-scanf-variant-selection")
  # The variant dir only holds lib/ and share/; headers live in the shared
  # arm-none-eabi/include dir that multilib.yaml's IncludeDirs would normally
  # add. The libc++ dir must precede the C dir so libc++'s wrapper headers
  # (which #include_next the newlib ones) are found first.
  set(STARM_C_INCLUDE_FLAGS "-isystem \"${STARM_RUNTIME_ROOT}/arm-none-eabi/include\"")
  set(STARM_CXX_INCLUDE_FLAGS "-isystem \"${STARM_RUNTIME_ROOT}/arm-none-eabi/include/c++/v1\" ${STARM_C_INCLUDE_FLAGS}")
  # CMake locates this with a flag-less `-print-file-name`, which would go
  # through multilib.yaml and pick the wrong (first) variant.
  set(CMAKE_CXX_STDLIB_MODULES_JSON "${STARM_RUNTIME_SYSROOT}/lib/libc++.modules.json")
endif()

# MCU specific flags
# --target must be explicit: starm-clang defaults to it (arm-st-none-eabi)
# internally, but the separately-installed clang-scan-deps (needed for C++
# module dependency scanning) is a generic host build that otherwise
# defaults to the host triple and rejects -mcpu.
set(TARGET_FLAGS "--target=arm-st-none-eabi -mcpu=cortex-m0plus ${TOOLCHAIN_MULTILIBS}")
set(COMMON_FLAGS "-Wall -fdata-sections -ffunction-sections -ftls-model=local-exec -fstack-usage")

set(CMAKE_ASM_FLAGS "${TARGET_FLAGS} ${STARM_C_INCLUDE_FLAGS} -x assembler-with-cpp -MP")
set(CMAKE_C_FLAGS "${TARGET_FLAGS} ${STARM_C_INCLUDE_FLAGS} ${COMMON_FLAGS}")
# CMake detects CMAKE_CXX_STANDARD_LIBRARY (required for `import std`) by
# preprocessing a probe header with CMAKE_CXX_FLAGS, so -stdlib=libc++ and the
# libc++ include dir have to be in here, not only on individual targets.
# -Wno-reserved-module-identifier: CMake compiles libc++'s own std.cppm with
# these flags, and naming a module `std` warns by default.
set(CMAKE_CXX_FLAGS "${TARGET_FLAGS} ${STARM_CXX_INCLUDE_FLAGS} ${COMMON_FLAGS} -stdlib=libc++ -fno-rtti -fno-exceptions -fno-threadsafe-statics -Wno-reserved-module-identifier")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-Oz -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Oz -g0")

# clang-scan-deps is required for CMake's C++ module dependency scanning
# (-format=p1689, needs Clang >= 16) but is not shipped in the st-arm-clang
# bundle. Point at a version-matched one installed separately (see
# https://apt.llvm.org -- `clang-tools-21` for this toolchain's Clang 21.1.1).
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "/usr/bin/clang-scan-deps-21")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS} ${TOOLCHAIN_LINK_FLAGS} -stdlib=libc++")

if (STARM_TOOLCHAIN_CONFIG STREQUAL "STARM_HYBRID")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --gcc-specs=nano.specs")
  set(TOOLCHAIN_LINK_LIBRARIES "m")
elseif(STARM_TOOLCHAIN_CONFIG STREQUAL "STARM_NEWLIB")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lcrt0-nosys")
elseif(STARM_TOOLCHAIN_CONFIG STREQUAL "STARM_PICOLIBC")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lcrt0-hosted -z norelro")

endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32G030xx_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -z noexecstack")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage ")
