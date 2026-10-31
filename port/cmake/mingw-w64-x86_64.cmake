# Cross-compiling the Windows build on Linux with MinGW-w64 (64-bit):
#
#   cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMP_PORTABLE=ON
#   cmake --build build-windows -j --target portable_zip
#
# Needs the MinGW-w64 GCC with POSIX threads (std::mutex):
#   Arch:   sudo pacman -S mingw-w64-gcc
#   Fedora: sudo dnf install mingw64-gcc-c++
#   Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix   (the -posix one, not -win32)
# Wine is optional: with it the unit tests run under Wine (ctest, or `wine build-windows/monsterparty_tests.exe`).
#
# Another prefix (e.g. llvm-mingw's clang) can be chosen with -DMINGW_PREFIX=... and
# -DMINGW_CXX=x86_64-w64-mingw32-clang++.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32 CACHE STRING "MinGW-w64 tool prefix")
set(MINGW_CXX ${MINGW_PREFIX}-g++ CACHE STRING "MinGW-w64 C++ compiler")
string(REGEX REPLACE "\\+\\+$" "" _mingw_cc_guess "${MINGW_CXX}")   # g++ -> g, clang++ -> clang
if(_mingw_cc_guess MATCHES "-g$")
  set(_mingw_cc_guess "${MINGW_PREFIX}-gcc")
endif()
set(MINGW_CC ${_mingw_cc_guess} CACHE STRING "MinGW-w64 C compiler")

# the -posix variants first (Debian/Ubuntu install both and point the plain names at -win32)
find_program(_mp_cxx NAMES ${MINGW_CXX}-posix ${MINGW_CXX})
find_program(_mp_cc NAMES ${MINGW_CC}-posix ${MINGW_CC})
if(NOT _mp_cxx OR NOT _mp_cc)
  message(FATAL_ERROR "No MinGW-w64 compiler (${MINGW_CXX}). Install it (Arch: sudo pacman -S mingw-w64-gcc; "
    "Fedora: sudo dnf install mingw64-gcc-c++; Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix), "
    "delete this build folder and configure again.")
endif()
find_program(_mp_rc NAMES ${MINGW_PREFIX}-windres windres llvm-windres)
set(CMAKE_CXX_COMPILER ${_mp_cxx})
set(CMAKE_C_COMPILER ${_mp_cc})
if(_mp_rc)
  set(CMAKE_RC_COMPILER ${_mp_rc})
endif()

# Libraries and headers come only from the MinGW sysroot (and what CMake fetches), never from
# the Linux system; programs (python3, git) from the host.
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX} /usr/lib/mxe/usr/${MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# run the Windows executables (the unit tests) under Wine when it's installed
find_program(_mp_wine NAMES wine64 wine)
if(_mp_wine)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_mp_wine})
endif()
