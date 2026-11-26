cmake_minimum_required(VERSION 3.25)

project(SpinDeckJuce VERSION 3.0.0 LANGUAGES C CXX OBJCXX)

include(FetchContent)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Apple architectures")

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(signalsmith-stretch)

# Use the SQLCipher amalgamation published with libdjinterop. The rest of the
# dependency is excluded. CommonCrypto is provided by macOS.
FetchContent_Declare(
    onelibrary_sqlcipher
    GIT_REPOSITORY https://github.com/xsco/libdjinterop.git
    GIT_TAG d61727c5a3369f79d11b70afba6c925ddeb7f54c
    SOURCE_SUBDIR ext
)
FetchContent_MakeAvailable(onelibrary_sqlcipher)
set(sqlcipher_dir "${onelibrary_sqlcipher_SOURCE_DIR}/ext/sqlcipher")
add_library(spindeck_sqlcipher STATIC "${sqlcipher_dir}/sqlite3.c")
target_include_directories(spindeck_sqlcipher PUBLIC "${sqlcipher_dir}")
target_compile_definitions(spindeck_sqlcipher PRIVATE
    SQLITE_HAS_CODEC SQLCIPHER_CRYPTO_CC SQLITE_EXTRA_INIT=sqlcipher_extra_init
    SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown SQLITE_TEMP_STORE=2 SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(spindeck_sqlcipher PUBLIC "-framework CoreFoundation" "-framework Security")
set_target_properties(spindeck_sqlcipher PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(spindeck_audio STATIC
    src/audio/BeatAnalysis.cpp
    src/audio/DeckEngine.cpp
    src/audio/FourDeckMixer.cpp
    src/audio/PluginHostService.cpp
    src/audio/TrackMetadata.cpp
    src/audio/StemFile.mm
    src/audio/TrackTags.mm
    src/audio/StemMaker.cpp
    src/control/T7Platter.cpp
    src/control/T7Midi.cpp
    src/control/T7Performance.cpp
)

target_include_directories(spindeck_audio PUBLIC src)
set_source_files_properties(src/audio/StemFile.mm src/audio/TrackTags.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(spindeck_audio PRIVATE "-framework AVFoundation" "-framework CoreMedia")
target_compile_definitions(spindeck_audio
    PUBLIC
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)
target_link_libraries(spindeck_audio
    PRIVATE signalsmith-stretch juce::juce_dsp
    PUBLIC
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
)

juce_add_gui_app(SpinDeck
    PRODUCT_NAME "Spin Deck"
    BUNDLE_ID "com.xtragoodlabs.spindeck.juce"
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED TRUE
)

target_sources(SpinDeck PRIVATE
    src/Application.cpp
    src/MainComponent.cpp
    src/CrateLibraryStore.cpp
    src/CrateViews.cpp
    src/GoodMixxHistory.cpp
    src/MixxxImport.cpp
    src/OneLibraryImport.cpp
    src/StemQueueWindow.cpp
    src/MainComponent.h
)
foreach(script IN ITEMS spindeck_stems.py spindeck_stems_setup.sh)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/../res/spindeck/${script}")
    set_source_files_properties("${source}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(SpinDeck PRIVATE "${source}")
endforeach()
foreach(dependency IN ITEMS signalsmith-stretch signalsmith-linear)
    FetchContent_GetProperties(${dependency} SOURCE_DIR dependencySource)
    set(notice "${CMAKE_CURRENT_BINARY_DIR}/${dependency}-LICENSE.txt")
    configure_file("${dependencySource}/LICENSE.txt" "${notice}" COPYONLY)
    set_source_files_properties("${notice}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
    target_sources(SpinDeck PRIVATE "${notice}")
endforeach()
set(sqlcipher_notice "${CMAKE_CURRENT_SOURCE_DIR}/licenses/SQLCipher.txt")
set_source_files_properties("${sqlcipher_notice}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
target_sources(SpinDeck PRIVATE "${sqlcipher_notice}")
target_link_libraries(SpinDeck
    PRIVATE
        spindeck_audio
        spindeck_sqlcipher
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(SpinDeck)

if(BUILD_TESTING)
    add_executable(spindeck_audio_tests tests/AudioEngineTests.cpp)
    target_link_libraries(spindeck_audio_tests PRIVATE spindeck_audio)
    add_test(NAME spindeck_audio_tests COMMAND spindeck_audio_tests)
    add_test(NAME spindeck_stem_playback COMMAND spindeck_audio_tests --test-stems)

    add_executable(spindeck_audio_benchmark tests/AudioEngineBenchmark.cpp)
    target_link_libraries(spindeck_audio_benchmark PRIVATE spindeck_audio)
endif()
