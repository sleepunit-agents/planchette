# The plugin (SPEC §6): a thin JUCE layer over the engine. Built only with -DHOCKET_PLUGIN=ON, so the engine's own
# build and tests never need JUCE.
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Everything but the format glue: the processor, the editor and its screens. The tests build it in too.
set(HOCKET_APP_SOURCES Processor.cpp Editor.cpp SongJson.cpp Clip.cpp Library.cpp ui/ClipButton.cpp
  ui/Controls.cpp ui/Views.cpp ui/PlayPage.cpp ui/GroupsPage.cpp ui/MatrixPage.cpp ui/SongPage.cpp ui/FormPage.cpp)

# The desktop: VST3 everywhere and AU on a Mac, as an instrument that makes MIDI
# (Live loads third-party MIDI generators as instruments as of Live 12, §6.2).
# The iPad (cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS): an AUv3 MIDI processor, the slot AUM and Loopy Pro
# route MIDI from, in the small app every AUv3 ships inside.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(HOCKET_FORMATS AUv3 Standalone)
  set(HOCKET_MIDI_EFFECT TRUE)
  set(HOCKET_SYNTH FALSE)
  set(HOCKET_AU_TYPE kAudioUnitType_MIDIProcessor)
else()
  set(HOCKET_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND HOCKET_FORMATS AU)
  endif()
  set(HOCKET_MIDI_EFFECT FALSE)
  set(HOCKET_SYNTH TRUE)
  set(HOCKET_AU_TYPE kAudioUnitType_MusicDevice)
endif()

juce_add_plugin(PlanchettePlugin
  PRODUCT_NAME "Planchette"
  COMPANY_NAME "sleepunit"
  BUNDLE_ID "com.sleepunit.planchette"
  PLUGIN_MANUFACTURER_CODE Slpu
  PLUGIN_CODE Plnc
  FORMATS ${HOCKET_FORMATS}
  IS_SYNTH ${HOCKET_SYNTH}
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT ${HOCKET_MIDI_EFFECT}
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Generator
  AU_MAIN_TYPE ${HOCKET_AU_TYPE})

target_sources(PlanchettePlugin PRIVATE ${HOCKET_APP_SOURCES})
target_compile_definitions(PlanchettePlugin PUBLIC
  JUCE_CONTENT_SHARING=$<BOOL:${HOCKET_MIDI_EFFECT}>
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(PlanchettePlugin
  PRIVATE planchette_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# A host in miniature: loads the built plugin, plays 16 bars, and checks its MIDI against the engine (hosttest.cpp).
juce_add_console_app(PlanchetteHostTest PRODUCT_NAME "planchette_hosttest")
target_sources(PlanchetteHostTest PRIVATE hosttest.cpp)
target_compile_definitions(PlanchetteHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PlanchetteHostTest PRIVATE planchette_engine juce::juce_audio_processors juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The editor drawn to a PNG while playing, without a host (snapshot.cpp).
juce_add_console_app(PlanchetteSnapshot PRODUCT_NAME "planchette_snapshot")
target_sources(PlanchetteSnapshot PRIVATE snapshot.cpp ${HOCKET_APP_SOURCES})
target_compile_definitions(PlanchetteSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PlanchetteSnapshot PRIVATE planchette_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The song document round trip (SongJson): a song with every kind of edit, to JSON and back, plays the same.
juce_add_console_app(PlanchetteDocTest PRODUCT_NAME "planchette_doctest")
target_sources(PlanchetteDocTest PRIVATE doctest.cpp SongJson.cpp Clip.cpp Library.cpp)
target_compile_definitions(PlanchetteDocTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  HOCKET_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
target_link_libraries(PlanchetteDocTest PRIVATE planchette_engine juce::juce_core juce::juce_audio_basics
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The screens driven like a finger would (uitest.cpp): press the buttons, check the song changed.
juce_add_console_app(PlanchetteUiTest PRODUCT_NAME "planchette_uitest")
target_sources(PlanchetteUiTest PRIVATE uitest.cpp ${HOCKET_APP_SOURCES})
target_compile_definitions(PlanchetteUiTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PlanchetteUiTest PRIVATE planchette_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# MIDI out to a port that comes and goes (porttest.cpp): connect, let go, reconnect, deliver.
juce_add_console_app(PlanchettePortTest PRODUCT_NAME "planchette_porttest")
target_sources(PlanchettePortTest PRIVATE porttest.cpp ${HOCKET_APP_SOURCES})
target_compile_definitions(PlanchettePortTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PlanchettePortTest PRIVATE planchette_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
