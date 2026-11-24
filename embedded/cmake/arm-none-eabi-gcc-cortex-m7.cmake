# GNU Arm Embedded toolchain for Cortex-M7 (STM32H743 and similar).
#
# Usage:
#   cmake -S embedded -B build-m7 \
#       -DCMAKE_TOOLCHAIN_FILE=embedded/cmake/arm-none-eabi-gcc-cortex-m7.cmake \
#       -DBALTIC_EMBEDDED_BUILD_TESTS=OFF
#
# The compiler is built for a generic bare-metal target; no C library is
# required by baltic_embedded.  Link the application with its own startup
# code and linker script.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_TOOLCHAIN_PREFIX "arm-none-eabi-" CACHE STRING
    "Prefix of the GNU Arm Embedded toolchain binaries")

set(CMAKE_C_COMPILER "${ARM_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR "${ARM_TOOLCHAIN_PREFIX}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ARM_TOOLCHAIN_PREFIX}ranlib" CACHE FILEPATH "")

set(CORTEX_M7_FLAGS
    "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT
    "${CORTEX_M7_FLAGS} -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CORTEX_M7_FLAGS} -Wl,--gc-sections")

# STM32H743 has 2 MiB of Flash; favour speed over the last few bytes.
set(BALTIC_EMBEDDED_OPT_FLAGS "-O2" CACHE STRING "" FORCE)
