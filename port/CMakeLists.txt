cmake_minimum_required(VERSION 3.18)
project(monsterparty_port CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Portable build (-DMP_PORTABLE=ON), to give to other people: SDL3 is built in (static) and so is the
# C++ runtime, and the data is looked for only next to the executable. The build makes a folder,
# build/monsterparty-portable/, holding the executable with palettes/, soundfonts/, shaders/ and an
# empty roms/; `cmake --build build --target portable_tar` packs it as a .tar.gz.
#
# Windows: the same, cross-compiled on Linux with MinGW-w64 (cmake/mingw-w64-x86_64.cmake has the
# packages to install). The folder is build-windows/monsterparty-windows/ and the target that packs
# it is portable_zip:
#   cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMP_PORTABLE=ON
#   cmake --build build-windows -j --target portable_zip
option(MP_PORTABLE "Self-contained build: bundled SDL3, data folders next to the executable" OFF)

if(MINGW)
  # one .exe with no MinGW DLLs next to it (libstdc++, libgcc, winpthread)
  add_link_options(-static)
  # the generated banks make more sections than an object file normally holds
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wa,-mbig-obj)
  endif()
endif()

# SDL3: use an installed copy if there is one (not in a portable build), otherwise build it from the
# libsdl-org GitHub repository. SDL loads X11, Wayland, PipeWire, PulseAudio etc. at run time, but
# only the ones whose development headers were installed when it was built are included.
if(NOT MP_PORTABLE AND NOT CMAKE_CROSSCOMPILING)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT SDL3_FOUND)
  include(FetchContent)
  message(STATUS "SDL3 not installed: fetching libsdl-org/SDL release-3.2.30")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.2.30 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

# A portable build is only as good as the SDL inside it: SDL includes a display or sound system only
# if its development headers were installed when it was built. Check the drivers it found.
if(MP_PORTABLE)
  if(SDL3_FOUND)
    message(FATAL_ERROR "MP_PORTABLE needs the bundled SDL3 (fresh build folder)")
  endif()
  set(_missing "")
  if(WIN32)
    set(_drivers VIDEO_DRIVER_WINDOWS AUDIO_DRIVER_WASAPI)   # always there with MinGW-w64
  else()
    set(_drivers VIDEO_DRIVER_X11 VIDEO_DRIVER_WAYLAND AUDIO_DRIVER_PIPEWIRE AUDIO_DRIVER_PULSEAUDIO AUDIO_DRIVER_ALSA)
  endif()
  foreach(_drv ${_drivers})
    get_directory_property(_on DIRECTORY ${sdl3_SOURCE_DIR} DEFINITION SDL_${_drv})
    if(NOT _on)
      string(APPEND _missing " ${_drv}")
    endif()
  endforeach()
  if(_missing)
    option(MP_PORTABLE_ALLOW_MISSING "Make a portable build even without some of SDL's drivers" OFF)
    set(_msg "The bundled SDL3 was configured without: ${_missing}.\n"
      "Install the development packages, delete this build folder and configure again:\n"
      "  Arch:   sudo pacman -S --needed base-devel cmake libx11 libxext libxcursor libxi libxrandr libxss libxkbcommon wayland wayland-protocols libdecor pipewire libpulse alsa-lib mesa\n"
      "  Fedora (on Bazzite: in a Fedora distrobox): sudo dnf install gcc-c++ cmake libX11-devel libXext-devel libXcursor-devel libXi-devel libXrandr-devel libXScrnSaver-devel libxkbcommon-devel wayland-devel wayland-protocols-devel libdecor-devel pipewire-devel pulseaudio-libs-devel alsa-lib-devel mesa-libGL-devel mesa-libEGL-devel\n"
      "  Debian/Ubuntu: sudo apt install build-essential cmake libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libgl-dev libegl-dev\n"
      "(-DMP_PORTABLE_ALLOW_MISSING=ON builds anyway.)")
    if(MP_PORTABLE_ALLOW_MISSING)
      message(WARNING ${_msg})
    else()
      message(FATAL_ERROR ${_msg})
    endif()
  endif()
endif()

include(FetchContent)

# MIDI music (optional, chosen in the settings window): TinySoundFont (MIT, header only) plays the
# music through a SoundFont. GeneralUser GS v2.0.3 by S. Christian Collins (free to use and
# redistribute, see its documentation/LICENSE.txt) is downloaded as the bundled SoundFont; it's
# read at run time only. Turn the download off with -DMP_FETCH_SOUNDFONT=OFF and put your own .sf2
# files in ~/.config/monsterparty/soundfonts/ instead.
FetchContent_Declare(tsf
  GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
  GIT_TAG 853a0a171759f1ddba0de1442133a75912bbeffa
  SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(tsf)
option(MP_FETCH_SOUNDFONT "Download the GeneralUser GS SoundFont (about 32 MB) for MIDI music" ON)
if(MP_FETCH_SOUNDFONT)
  FetchContent_Declare(generaluser_gs
    GIT_REPOSITORY https://github.com/mrbumpy409/GeneralUser-GS.git
    GIT_TAG 684543d5e5efaef08d02be50dcda8d552478fa60
    SOURCE_SUBDIR no-cmake-build)
  FetchContent_MakeAvailable(generaluser_gs)
endif()

# Launcher UI: Dear ImGui (MIT), drawn with SDL's renderer. Only its core and SDL3 backend files are built.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9b GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# Optional shaders: libretro's GLSL shader collection, read at run time (never compiled in). Each
# shader keeps its own license (many are GPL); see README.md. Pinned to a known commit. A portable
# build only hands out the ones in dist/shaders.txt.
FetchContent_Declare(glsl_shaders
  GIT_REPOSITORY https://github.com/libretro/glsl-shaders.git
  GIT_TAG cba01a48b0974a1232bea68d999a7c21bc4c233e
  SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(glsl_shaders)
# stb_image (public domain / MIT) to load the shaders' PNG lookup textures
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 013ac3beddff3dbffafd5177e7972067cd2b5083
  SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(stb)

# The game itself (the translated code and the runtime it needs) is a library, shared by the game
# and the unit tests. gen/ (US release) and gen_jp/ (Japanese prototype, namespace jp) are produced
# by tools/translate.py from monsterparty/ and monsterparty_jp/. They are not kept in git: the
# translation is made here, whenever the disassembly or translate.py is newer than the output.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MP_GEN_FILES_US)
set(MP_GEN_FILES_JP)
foreach(_f bank4.cpp bank5.cpp bank7.cpp ram_symbols.h rom_symbols.h)
  list(APPEND MP_GEN_FILES_US ${CMAKE_CURRENT_SOURCE_DIR}/gen/${_f})
  list(APPEND MP_GEN_FILES_JP ${CMAKE_CURRENT_SOURCE_DIR}/gen_jp/${_f})
endforeach()
set(MP_ASM_US ${CMAKE_CURRENT_SOURCE_DIR}/../monsterparty)
set(MP_ASM_JP ${CMAKE_CURRENT_SOURCE_DIR}/../monsterparty_jp)
add_custom_command(OUTPUT ${MP_GEN_FILES_US}
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/translate.py ${MP_ASM_US} ${CMAKE_CURRENT_SOURCE_DIR}/gen
  DEPENDS ${MP_ASM_US}/monsterparty.asm ${MP_ASM_US}/bank4.asm ${MP_ASM_US}/bank5.asm ${MP_ASM_US}/bank7.asm
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/translate.py
  COMMENT "Translating monsterparty/ to gen/")
add_custom_command(OUTPUT ${MP_GEN_FILES_JP}
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/translate.py ${MP_ASM_JP} ${CMAKE_CURRENT_SOURCE_DIR}/gen_jp --variant jp
  DEPENDS ${MP_ASM_JP}/monsterparty.asm ${MP_ASM_JP}/bank4.asm ${MP_ASM_JP}/bank5.asm ${MP_ASM_JP}/bank7.asm
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/translate.py
  COMMENT "Translating monsterparty_jp/ to gen_jp/")
add_custom_target(mp_gen DEPENDS ${MP_GEN_FILES_US} ${MP_GEN_FILES_JP})
add_library(mpcore STATIC
  src/platform.cpp src/nes.cpp src/audio.cpp src/settings.cpp src/palette.cpp src/music.cpp src/remix.cpp src/leaderboard.cpp src/language.cpp
  gen/bank4.cpp gen/bank5.cpp gen/bank7.cpp
  gen_jp/bank4.cpp gen_jp/bank5.cpp gen_jp/bank7.cpp)
add_dependencies(mpcore mp_gen)   # (the generated headers are needed by everything that uses mpcore)
target_include_directories(mpcore PUBLIC src gen ${stb_SOURCE_DIR} ${tsf_SOURCE_DIR})
target_link_libraries(mpcore PUBLIC SDL3::SDL3)
add_executable(monsterparty src/main.cpp src/launcher.cpp src/gl_api.cpp src/shader_chain.cpp src/render3d.cpp)
if(WIN32)
  # A GUI program: no console window opens with it. Started from a console it still prints there;
  # otherwise the output goes to monsterparty.log in the settings folder (mp_setup_console()).
  # windows/monsterparty.rc: the manifest (UTF-8 code page, Windows 10/11) and version information,
  # plus the icon when there is a graphics/icon.png (made into an .ico by tools/make_icon.py).
  set_target_properties(monsterparty PROPERTIES WIN32_EXECUTABLE ON)
  enable_language(RC)
  set(MP_ICON_LINE "")
  set(MP_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/windows/monsterparty.manifest)
  set(_rc_deps ${MP_MANIFEST})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/graphics/icon.png)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/monsterparty.ico
      COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_icon.py
              ${CMAKE_CURRENT_SOURCE_DIR}/graphics/icon.png ${CMAKE_CURRENT_BINARY_DIR}/monsterparty.ico
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/graphics/icon.png ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_icon.py
      COMMENT "Making monsterparty.ico from graphics/icon.png")
    set(MP_ICON_LINE "1 ICON \"${CMAKE_CURRENT_BINARY_DIR}/monsterparty.ico\"")
    list(APPEND _rc_deps ${CMAKE_CURRENT_BINARY_DIR}/monsterparty.ico)
  endif()
  configure_file(windows/monsterparty.rc.in ${CMAKE_CURRENT_BINARY_DIR}/monsterparty.rc @ONLY)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/monsterparty.rc PROPERTIES OBJECT_DEPENDS "${_rc_deps}")
  target_sources(monsterparty PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/monsterparty.rc)
endif()
if(NOT MP_PORTABLE)
  # where to find things when there's no folder of that name next to the executable
  target_compile_definitions(mpcore PRIVATE
    MP_PALETTE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/palettes")  # bundled palettes
  target_compile_definitions(monsterparty PRIVATE
    MP_SHADER_DIR="${glsl_shaders_SOURCE_DIR}"
    MP_ROM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../roms")       # the repository's roms/
  if(MP_FETCH_SOUNDFONT)
    target_compile_definitions(mpcore PRIVATE MP_SOUNDFONT_DIR="${generaluser_gs_SOURCE_DIR}")   # the bundled SoundFont
  endif()
endif()
target_link_libraries(monsterparty PRIVATE mpcore imgui)
# the generated bank functions are large; -O1 keeps compile times reasonable while staying fast
set_source_files_properties(gen/bank4.cpp gen/bank5.cpp gen/bank7.cpp gen_jp/bank4.cpp gen_jp/bank5.cpp gen_jp/bank7.cpp
  PROPERTIES COMPILE_OPTIONS "-O1")

# Unit tests (Catch2 v3, BSL-1.0), not in a portable build: tests/, run with `ctest` or
# ./build/monsterparty_tests (see tests/README.md). They need the ROMs in the repository's roms/.
if(NOT MP_PORTABLE)
  option(MP_BUILD_TESTS "Build the unit tests (fetches Catch2)" ON)
endif()
if(MP_BUILD_TESTS AND NOT MP_PORTABLE)
  FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.8.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  add_executable(monsterparty_tests
    tests/support.cpp tests/test_rom.cpp tests/test_emulation.cpp tests/test_options.cpp
    tests/test_remix.cpp tests/test_bosses.cpp tests/test_trainers.cpp tests/test_files.cpp tests/test_3d.cpp tests/test_widescreen.cpp tests/test_leaderboard.cpp tests/test_language.cpp)
  target_link_libraries(monsterparty_tests PRIVATE mpcore Catch2::Catch2WithMain)
  target_compile_definitions(monsterparty_tests PRIVATE
    MP_TEST_ROM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../roms"
    MP_TEST_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden.txt")
  include(CTest)
  include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
  if(CMAKE_CROSSCOMPILING)
    catch_discover_tests(monsterparty_tests DISCOVERY_MODE PRE_TEST)   # under Wine, when ctest runs, not at build time
  else()
    catch_discover_tests(monsterparty_tests)
  endif()
endif()

if(MP_PORTABLE)
  # The folder to hand out. The executable is built straight into it. The shaders and the SoundFont
  # (pinned, and large) are copied when CMake configures; the palettes on every build.
  if(WIN32)
    set(MP_DIST_NAME monsterparty-windows)
  else()
    set(MP_DIST_NAME monsterparty-portable)
  endif()
  set(MP_DIST ${CMAKE_BINARY_DIR}/${MP_DIST_NAME})
  set_target_properties(monsterparty PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${MP_DIST})
  if(MINGW)
    target_link_options(monsterparty PRIVATE -s)   # -static is set for every MinGW target above
  else()
    target_link_options(monsterparty PRIVATE -static-libstdc++ -static-libgcc -s)
  endif()
  # Only the shaders the launcher offers (and what they need: passes, lookup textures, licence
  # files) are handed out, not the whole collection. dist/shaders.txt is made by tools/list_shaders.py.
  file(REMOVE_RECURSE ${MP_DIST}/shaders)
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/dist/shaders.txt MP_SHADER_FILES)
  foreach(_f ${MP_SHADER_FILES})
    get_filename_component(_d ${_f} DIRECTORY)
    file(COPY ${glsl_shaders_SOURCE_DIR}/${_f} DESTINATION ${MP_DIST}/shaders/${_d})
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dist/shaders.txt)
  if(MP_FETCH_SOUNDFONT)
    file(COPY ${generaluser_gs_SOURCE_DIR}/GeneralUser-GS.sf2 DESTINATION ${MP_DIST}/soundfonts)
    file(COPY ${generaluser_gs_SOURCE_DIR}/documentation/LICENSE.txt DESTINATION ${MP_DIST}/soundfonts)
    file(RENAME ${MP_DIST}/soundfonts/LICENSE.txt ${MP_DIST}/soundfonts/GeneralUser-GS-LICENSE.txt)
  endif()
  file(MAKE_DIRECTORY ${MP_DIST}/roms)
  file(WRITE ${MP_DIST}/roms/PUT-ROMS-HERE.txt
    "Put your own copy of the game here, named exactly:\n"
    "     Monster Party (USA).nes                              (required) (No-Intro, CRC32 Headered:fe202c44 Headerless:02b9e7c2)\n"
    "     Parody World - Monster Party (Japan) (Proto).nes     (optional) (No-Intro, CRC32 Headered:dd7b44d6 Headerless:21e28f50)\n")
  if(WIN32)
    configure_file(dist/README-windows.txt ${MP_DIST}/README.txt COPYONLY)
  else()
    configure_file(dist/README.txt ${MP_DIST}/README.txt COPYONLY)
  endif()
  add_custom_command(TARGET monsterparty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${MP_DIST}/palettes
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/palettes ${MP_DIST}/palettes)
  if(WIN32)
    add_custom_target(portable_zip
      COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/${MP_DIST_NAME}.zip
      COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/${MP_DIST_NAME}.zip --format=zip ${MP_DIST_NAME}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR} DEPENDS monsterparty
      COMMENT "Packing ${MP_DIST_NAME}.zip")
  else()
    add_custom_target(portable_tar
      COMMAND ${CMAKE_COMMAND} -E tar czf ${CMAKE_BINARY_DIR}/monsterparty-portable.tar.gz monsterparty-portable
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR} DEPENDS monsterparty
      COMMENT "Packing build/monsterparty-portable.tar.gz")
  endif()
endif()
