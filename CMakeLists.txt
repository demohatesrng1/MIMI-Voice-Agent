cmake_minimum_required(VERSION 3.24)
# OBJC must be enabled alongside OBJCXX: with only OBJCXX, CMake routes .m
# files (ggml-metal.m) through the Objective-C++ compiler, which does not
# accept them.
project(mimi VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(NOT APPLE)
  message(FATAL_ERROR "Mimi targets macOS: it uses CoreAudio, AppleScript and Metal.")
endif()

option(MIMI_BUILD_GUI   "Build the Qt6 desktop app"        ON)
option(MIMI_BUILD_TOOLS "Build the command-line harnesses" ON)

include(FetchContent)

# ---------------------------------------------------------------- dependencies

# Single-header CoreAudio capture/playback. SOURCE_SUBDIR points at a path with
# no CMakeLists.txt so we get the sources without miniaudio's own project, which
# would claim the `miniaudio` target name and hunt for SDL/vorbis/opus.
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.22
  GIT_SHALLOW    ON
  SOURCE_SUBDIR  cmake-no-op)
FetchContent_MakeAvailable(miniaudio)

add_library(mimi_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(mimi_miniaudio SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")
target_compile_definitions(mimi_miniaudio PUBLIC
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
  MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH)
target_link_libraries(mimi_miniaudio PUBLIC
  "-framework CoreAudio" "-framework CoreFoundation" "-framework AudioToolbox"
  "-framework AudioUnit" pthread m)

find_package(nlohmann_json 3.11 REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

# whisper.cpp. The reason the transcriber is C++ at all: GGML's Metal backend
# runs the encoder on the M-series GPU, which CTranslate2 (faster-whisper)
# cannot do. EMBED_LIBRARY compiles the Metal shaders into the binary so a
# bundled app has no runtime file to find.
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(GGML_METAL              ON CACHE BOOL "" FORCE)
set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
set(GGML_ACCELERATE         ON CACHE BOOL "" FORCE)
set(GGML_BLAS              OFF CACHE BOOL "" FORCE)

# Header-only HTTP client. Ollama is on 127.0.0.1, so no TLS is needed and we
# can skip the OpenSSL dependency entirely.
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.5
  GIT_SHALLOW    ON
  SOURCE_SUBDIR  cmake-no-op)
FetchContent_MakeAvailable(httplib)

add_library(mimi_httplib INTERFACE)
target_include_directories(mimi_httplib SYSTEM INTERFACE "${httplib_SOURCE_DIR}")

FetchContent_Declare(whisper
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG        v1.7.6
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(whisper)

# ------------------------------------------------------------------ mimi_core

add_library(mimi_core STATIC
  src/core/log.cpp
  src/core/paths.cpp
)
target_include_directories(mimi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mimi_core PUBLIC nlohmann_json::nlohmann_json)

# ----------------------------------------------------------------- mimi_audio

add_library(mimi_audio STATIC
  src/audio/ring_buffer.cpp
  src/audio/capture.cpp
  src/audio/wav.cpp
)
target_include_directories(mimi_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mimi_audio PUBLIC mimi_core PRIVATE mimi_miniaudio)

# ----------------------------------------------------------------- mimi_voice

add_library(mimi_voice STATIC
  src/voice/onnx_model.cpp
  src/voice/vad.cpp
  src/voice/wake_word.cpp
  src/voice/stt.cpp
  src/voice/listener.cpp
  src/voice/phrase_spotter.cpp
  src/voice/tts.mm
  src/voice/voicevox.cpp
)
target_include_directories(mimi_voice PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mimi_voice PUBLIC mimi_audio onnxruntime::onnxruntime
  PRIVATE whisper mimi_brain mimi_httplib "-framework AVFoundation" "-framework Foundation")
set_source_files_properties(src/voice/tts.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc -std=c++20")

# ----------------------------------------------------------------- mimi_brain

add_library(mimi_brain STATIC
  src/brain/ollama.cpp
  src/brain/shell.cpp
  src/brain/tools.cpp
  src/brain/router.cpp
  src/brain/journal.cpp
  src/brain/reflect.cpp
)
target_include_directories(mimi_brain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mimi_brain PUBLIC mimi_core PRIVATE mimi_httplib)

# ----------------------------------------------------------------------- tools

if(MIMI_BUILD_TOOLS)
  add_executable(mimi_mic tools/mimi_mic.cpp)
  target_link_libraries(mimi_mic PRIVATE mimi_audio)

  add_executable(mimi_probe tools/mimi_probe.cpp)
  target_link_libraries(mimi_probe PRIVATE onnxruntime::onnxruntime)

  add_executable(mimi_wake tools/mimi_wake.cpp)
  target_link_libraries(mimi_wake PRIVATE mimi_voice)

  add_executable(mimi_listen tools/mimi_listen.cpp)
  target_link_libraries(mimi_listen PRIVATE mimi_voice)

  add_executable(mimi_say tools/mimi_say.cpp)
  target_link_libraries(mimi_say PRIVATE mimi_voice)

  add_executable(mimi_spot tools/mimi_spot.cpp)
  target_link_libraries(mimi_spot PRIVATE mimi_voice)

  add_executable(mimi_brain_cli tools/mimi_brain_cli.cpp)
  target_link_libraries(mimi_brain_cli PRIVATE mimi_brain)

  add_executable(mimi_tools tools/mimi_tools.cpp)
  target_link_libraries(mimi_tools PRIVATE mimi_brain)

  add_executable(mimi_route tools/mimi_route.cpp)
  target_link_libraries(mimi_route PRIVATE mimi_brain)
endif()

# ------------------------------------------------------------------- the app

if(MIMI_BUILD_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
  qt_standard_project_setup()

  qt_add_executable(mimi_app MACOSX_BUNDLE
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/floating_orb.cpp
    src/ui/skills_view.cpp
    src/ui/home_view.cpp
    src/ui/ambient.cpp
    src/ui/controls.cpp
    src/ui/command_bar.cpp
    src/ui/icons.cpp
    src/ui/mac_window.mm
    src/ui/voice_orb.cpp
    src/ui/voice_bridge.hpp     # Q_OBJECT, needs moc
    src/ui/main_window.hpp
    src/ui/floating_orb.hpp
    src/ui/skills_view.hpp
    src/ui/home_view.hpp
    src/ui/ambient.hpp
    src/ui/controls.hpp
    src/ui/command_bar.hpp
    src/ui/icons.hpp
    src/ui/mac_window.hpp
    src/ui/voice_orb.hpp
  )
  qt_add_resources(mimi_app "theme"
    PREFIX "/"
    BASE   "src/ui"
    FILES  src/ui/theme.qss)

  # The icon has to be copied into Contents/Resources and named in the plist;
  # setting only one of the two gives a bundle with a generic icon.
  set(MIMI_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/assets/Mimi.icns")
  target_sources(mimi_app PRIVATE "${MIMI_ICNS}")
  set_source_files_properties("${MIMI_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")

  qt_add_resources(mimi_app "appicon"
    PREFIX "/"
    BASE   "assets"
    FILES  assets/mimi_face.png)

  target_link_libraries(mimi_app PRIVATE Qt6::Widgets mimi_voice mimi_brain
    "-framework AppKit")

  set_source_files_properties(src/ui/mac_window.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -std=c++20")

  set_target_properties(mimi_app PROPERTIES
    OUTPUT_NAME "Mimi"
    MACOSX_BUNDLE_BUNDLE_NAME "Mimi"
    MACOSX_BUNDLE_ICON_FILE "Mimi.icns"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mimi.assistant"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
endif()

message(STATUS "")
message(STATUS "  Mimi ${PROJECT_VERSION}  (${CMAKE_BUILD_TYPE})")
message(STATUS "  onnxruntime : ${onnxruntime_VERSION}")
message(STATUS "  gui         : ${MIMI_BUILD_GUI}")
message(STATUS "")
