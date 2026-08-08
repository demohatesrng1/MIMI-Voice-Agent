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

# VOICEVOX CORE -- the offline neural Japanese TTS Mimi speaks with. Not fetched
# by CMake: it ships a prebuilt dylib + ONNX Runtime + Open JTalk dict + voice
# models, pulled by scripts/fetch_voicevox.sh into ./voicevox_core (gitignored).
# When that tree is absent the app still builds and falls back to AVSpeech, so
# this is entirely optional -- MIMI_HAS_VOICEVOX gates the code in voicevox.cpp.
set(MIMI_VOICEVOX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/voicevox_core"
    CACHE PATH "Root of the fetched VOICEVOX CORE tree")
set(MIMI_VOICEVOX_LIB "${MIMI_VOICEVOX_DIR}/c_api/lib/libvoicevox_core.dylib")
if(EXISTS "${MIMI_VOICEVOX_LIB}")
  set(MIMI_HAS_VOICEVOX ON)
  # The prebuilt dylib is stamped with the CI machine's absolute path as its
  # install name, which no consumer can resolve. Rewrite it to @rpath so the
  # binaries we link find it via the rpath we add below. Idempotent.
  execute_process(
    COMMAND install_name_tool -id "@rpath/libvoicevox_core.dylib" "${MIMI_VOICEVOX_LIB}"
    ERROR_QUIET)
  message(STATUS "  voicevox    : ${MIMI_VOICEVOX_DIR}")
else()
  set(MIMI_HAS_VOICEVOX OFF)
  message(STATUS "  voicevox    : not found (run scripts/fetch_voicevox.sh); using AVSpeech")
endif()

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

# VOICEVOX CORE. PUBLIC so the dylib link and the rpath propagate to every
# executable that links mimi_voice (the app and the CLI harnesses). The library
# dlopens its ONNX Runtime by path at runtime, so only the core dylib is linked.
if(MIMI_HAS_VOICEVOX)
  target_compile_definitions(mimi_voice PRIVATE VOICEVOX_LOAD_ONNXRUNTIME)
  # PUBLIC, not PRIVATE: the settings page reports which voice she actually
  # speaks in, and a definition scoped to this library made it report the
  # fallback on a build that had VOICEVOX all along.
  target_compile_definitions(mimi_voice PUBLIC MIMI_HAS_VOICEVOX)
  target_include_directories(mimi_voice SYSTEM PRIVATE "${MIMI_VOICEVOX_DIR}/c_api/include")
  # Full-path link: CMake propagates it to every executable that links
  # mimi_voice and auto-adds an rpath to the dylib's directory for the build tree.
  target_link_libraries(mimi_voice PUBLIC "${MIMI_VOICEVOX_LIB}")
endif()

# ----------------------------------------------------------------- mimi_brain

add_library(mimi_brain STATIC
  src/brain/ollama.cpp
  src/brain/shell.cpp
  src/brain/tools.cpp
  src/brain/router.cpp
  src/brain/journal.cpp
  src/brain/reflect.cpp
  src/brain/notes.cpp
  src/brain/account.cpp
  src/brain/accessibility.mm
)
target_include_directories(mimi_brain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mimi_brain PUBLIC mimi_core PRIVATE mimi_httplib)

# The Accessibility API lives in ApplicationServices; AppKit is what identifies
# the frontmost application to drive.
set_source_files_properties(src/brain/accessibility.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(mimi_brain PRIVATE
  "-framework ApplicationServices" "-framework AppKit" "-framework CoreGraphics"
  "-framework AVFoundation" "-framework Contacts" "-framework Security")

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

  add_executable(mimi_ax tools/mimi_ax.cpp)
  target_link_libraries(mimi_ax PRIVATE mimi_brain)

  add_executable(mimi_test tools/mimi_test.cpp)
  target_link_libraries(mimi_test PRIVATE mimi_brain)

  # The way back into a locked account. The GUI gate has no reset of its own,
  # and an assistant you cannot sign into is worse than one with no lock.
  add_executable(mimi_account tools/mimi_account.cpp)
  target_link_libraries(mimi_account PRIVATE mimi_brain)

  add_executable(mimi_route tools/mimi_route.cpp)
  target_link_libraries(mimi_route PRIVATE mimi_brain)
endif()

# ------------------------------------------------------------------- the app

if(MIMI_BUILD_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
  qt_standard_project_setup()

  # The 3D avatar. Rendered by three.js + three-vrm inside a Qt WebEngine page,
  # because three-vrm already implements MToon, the spring-bone simulation, the
  # expression manager and eye lookAt -- writing those four against Qt Quick 3D
  # would be a rendering project rather than a feature. Optional in exactly the
  # way VOICEVOX is: without WebEngine the app still builds and shows the 2D orb.
  option(MIMI_BUILD_AVATAR "Build the 3D VRM avatar (needs Qt WebEngine)" ON)
  set(MIMI_HAS_AVATAR OFF)
  if(MIMI_BUILD_AVATAR)
    find_package(Qt6 6.5 QUIET COMPONENTS WebEngineWidgets)
    if(Qt6WebEngineWidgets_FOUND)
      set(MIMI_HAS_AVATAR ON)
      message(STATUS "  avatar      : Qt WebEngine ${Qt6WebEngineWidgets_VERSION}")
    else()
      message(STATUS "  avatar      : Qt WebEngine not found; using the 2D orb")
    endif()
  endif()

  qt_add_executable(mimi_app MACOSX_BUNDLE
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/floating_orb.cpp
    src/ui/skills_view.cpp
    src/ui/home_view.cpp
    src/ui/ambient.cpp
    src/ui/controls.cpp
    src/ui/command_bar.cpp
    src/ui/workspace_dock.cpp
    src/ui/timeline_view.cpp
    src/ui/live_thinking.cpp
    src/ui/context_ribbon.cpp
    src/ui/command_palette.cpp
    src/ui/glass_card.cpp
    src/ui/neural_search.cpp
    src/ui/ambient_notice.cpp
    src/ui/notes_view.cpp
    src/ui/account_view.cpp
    src/ui/settings_view.cpp
    src/ui/ambient_audio.mm
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
    src/ui/workspace_dock.hpp
    src/ui/timeline_view.hpp
    src/ui/live_thinking.hpp
    src/ui/context_ribbon.hpp
    src/ui/command_palette.hpp
    src/ui/glass_card.hpp
    src/ui/neural_search.hpp
    src/ui/ambient_notice.hpp
    src/ui/ambient_audio.hpp
    src/ui/presence.hpp
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

  # The avatar's own sources and its web payload. The .vrm is deliberately NOT
  # here: it is licensed for this user only, gitignored, and found on disk at
  # runtime -- see AvatarScheme::findModel().
  if(MIMI_HAS_AVATAR)
    target_sources(mimi_app PRIVATE
      src/ui/avatar_view.cpp
      src/ui/avatar_scheme.cpp
      src/ui/avatar_view.hpp      # Q_OBJECT, needs moc
      src/ui/avatar_scheme.hpp)   # Q_OBJECT, needs moc
    qt_add_resources(mimi_app "avatar"
      PREFIX "/avatar"
      BASE   "src/ui/web"
      FILES  src/ui/web/index.html src/ui/web/avatar.bundle.js)
    target_link_libraries(mimi_app PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(mimi_app PRIVATE MIMI_HAS_AVATAR)
  endif()

  set_source_files_properties(src/ui/mac_window.mm src/ui/ambient_audio.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -std=c++20")

  set_target_properties(mimi_app PROPERTIES
    OUTPUT_NAME "Mimi"
    MACOSX_BUNDLE_BUNDLE_NAME "Mimi"
    MACOSX_BUNDLE_ICON_FILE "Mimi.icns"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mimi.assistant"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")

  # Offscreen screenshot harness: renders the UI widgets in isolation (no native
  # chrome, mic or models) so the drawing can be verified without launching the
  # always-on app. Shares the app's own widget sources.
  if(MIMI_BUILD_TOOLS)
    qt_add_executable(mimi_gallery
      tools/mimi_gallery.cpp
      # The app's own widgets, so the gallery renders exactly what ships --
      # including the whole window, chrome and all.
      src/ui/main_window.cpp
      src/ui/home_view.cpp
      src/ui/notes_view.cpp
      src/ui/account_view.cpp
    src/ui/account_view.cpp
      src/ui/settings_view.cpp
      src/ui/timeline_view.cpp
      src/ui/command_bar.cpp
      src/ui/command_palette.cpp
      src/ui/neural_search.cpp
      src/ui/context_ribbon.cpp
      src/ui/workspace_dock.cpp
      src/ui/live_thinking.cpp
      src/ui/floating_orb.cpp
      src/ui/voice_orb.cpp
      src/ui/glass_card.cpp
      src/ui/ambient.cpp
      src/ui/controls.cpp
      src/ui/icons.cpp
      src/ui/mac_window.mm
      src/ui/ambient_audio.mm
      # Headers carrying Q_OBJECT, for AUTOMOC.
      src/ui/main_window.hpp
      src/ui/home_view.hpp
      src/ui/notes_view.hpp
      src/ui/account_view.hpp
    src/ui/account_view.hpp
      src/ui/settings_view.hpp
      src/ui/timeline_view.hpp
      src/ui/command_bar.hpp
      src/ui/command_palette.hpp
      src/ui/neural_search.hpp
      src/ui/context_ribbon.hpp
      src/ui/workspace_dock.hpp
      src/ui/live_thinking.hpp
      src/ui/floating_orb.hpp
      src/ui/voice_orb.hpp
      src/ui/glass_card.hpp
      src/ui/ambient.hpp
      src/ui/controls.hpp
      src/ui/voice_bridge.hpp
    )
    qt_add_resources(mimi_gallery "galleryicon"
      PREFIX "/" BASE "assets" FILES assets/mimi_face.png)
    target_link_libraries(mimi_gallery PRIVATE Qt6::Widgets mimi_voice mimi_brain)
    target_compile_definitions(mimi_gallery PRIVATE
      MIMI_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The avatar on its own, the way every other layer has a harness. Not part
    # of mimi_gallery: that runs offscreen, and WebEngine needs a real GPU
    # surface, so the two cannot share a process.
    if(MIMI_HAS_AVATAR)
      qt_add_executable(mimi_avatar
        tools/mimi_avatar.cpp
        src/ui/avatar_view.cpp
        src/ui/avatar_scheme.cpp
        src/ui/avatar_view.hpp
        src/ui/avatar_scheme.hpp)
      qt_add_resources(mimi_avatar "avatar_harness"
        PREFIX "/avatar"
        BASE   "src/ui/web"
        FILES  src/ui/web/index.html src/ui/web/avatar.bundle.js)
      # mimi_voice, not mimi_core: presence.hpp includes voice/listener.hpp for
      # the State enum it is a superset of, which drags in onnxruntime's headers.
      target_link_libraries(mimi_avatar PRIVATE Qt6::Widgets Qt6::WebEngineWidgets mimi_voice)
      target_compile_definitions(mimi_avatar PRIVATE MIMI_HAS_AVATAR)
    endif()
  endif()
endif()

message(STATUS "")
message(STATUS "  Mimi ${PROJECT_VERSION}  (${CMAKE_BUILD_TYPE})")
message(STATUS "  onnxruntime : ${onnxruntime_VERSION}")
message(STATUS "  gui         : ${MIMI_BUILD_GUI}")
message(STATUS "")
