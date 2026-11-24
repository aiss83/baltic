# Clang toolchain for Cortex-M7 (STM32H743 and similar).
#
# Usage:
#   cmake -S embedded -B build-m7-clang \
#       -DCMAKE_TOOLCHAIN_FILE=embedded/cmake/arm-none-eabi-clang-cortex-m7.cmake \
#       -DBALTIC_EMBEDDED_BUILD_TESTS=OFF
#
# Requires a clang with the ARM target (e.g. upstream LLVM or Xcode clang).
# Only the static library is built; linking a firmware image is left to the
# application toolchain.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET arm-none-eabi)

# Xcode's clang does not ship llvm-ar, and BSD ar cannot create archives
# containing ELF objects.  Require LLVM binutils for a usable static library.
find_program(BALTIC_LLVM_AR NAMES llvm-ar)
find_program(BALTIC_LLVM_RANLIB NAMES llvm-ranlib)
if(NOT BALTIC_LLVM_AR OR NOT BALTIC_LLVM_RANLIB)
    message(FATAL_ERROR
        "llvm-ar and llvm-ranlib are required for the Clang cross build. "
        "Use the GNU Arm toolchain file, or point BALTIC_LLVM_AR and "
        "BALTIC_LLVM_RANLIB at LLVM binutils.")
endif()
set(CMAKE_AR "${BALTIC_LLVM_AR}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${BALTIC_LLVM_RANLIB}" CACHE FILEPATH "")

set(CORTEX_M7_FLAGS
    "--target=arm-none-eabi -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT
    "${CORTEX_M7_FLAGS} -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CORTEX_M7_FLAGS}")

set(BALTIC_EMBEDDED_OPT_FLAGS "-O2" CACHE STRING "" FORCE)
