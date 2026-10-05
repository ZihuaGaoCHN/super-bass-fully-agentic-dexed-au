cmake_minimum_required(VERSION 3.24)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(READ "${REPOSITORY_ROOT}/Source/security/CredentialStore_mac.mm" credential_source)
file(READ "${REPOSITORY_ROOT}/Source/ui/presets/PresetLibraryService.cpp" preset_source)
file(READ "${REPOSITORY_ROOT}/Tests/TestMessagePump.h" message_pump_source)
file(READ "${REPOSITORY_ROOT}/Tests/TestMain.cpp" test_main_source)
file(READ "${REPOSITORY_ROOT}/Tests/CMakeLists.txt" test_cmake)
file(READ "${REPOSITORY_ROOT}/Source/CMakeLists.txt" product_cmake)

if(credential_source MATCHES "static_cast<CFMutableDictionaryRef>\\(query\\.get\\(\\)\\)")
    message(FATAL_ERROR
        "The macOS credential store casts a const CFTypeRef to a mutable dictionary")
endif()
if(NOT credential_source MATCHES "auto\\* query = makeQuery\\(providerId\\);"
   OR NOT credential_source MATCHES "ScopedCF queryOwner\\(query\\);")
    message(FATAL_ERROR
        "The macOS credential query must retain its mutable type while using scoped ownership")
endif()
if(preset_source MATCHES "activeFileCartridge[ \t\r\n]*=[ \t\r\n]*\\{\\};")
    message(FATAL_ERROR
        "juce::File reset uses an ambiguous empty initializer under Apple Clang")
endif()
if(NOT preset_source MATCHES
   "activeFileCartridge[ \t\r\n]*=[ \t\r\n]*juce::File[ \t\r\n]*\\{\\};")
    message(FATAL_ERROR
        "The active cartridge file must be reset with an explicit juce::File value")
endif()
if(message_pump_source MATCHES "CoreFoundation"
   OR NOT message_pump_source MATCHES "runDispatchLoopUntil")
    message(FATAL_ERROR
        "The test message pump must use JUCE's complete platform dispatch loop")
endif()
if(NOT test_cmake MATCHES "JUCE_MODAL_LOOPS_PERMITTED=1")
    message(FATAL_ERROR
        "Only the test target must enable JUCE's bounded dispatch-loop helper")
endif()
if(product_cmake MATCHES "JUCE_MODAL_LOOPS_PERMITTED")
    message(FATAL_ERROR
        "Product targets must not enable JUCE modal loops")
endif()
string(FIND "${test_main_source}" "juce::initialiseNSApplication();"
       initialise_ns_application_position)
string(FIND "${test_main_source}" "juce::ScopedJuceInitialiser_GUI juceInitialiser;"
       scoped_juce_position)
if(initialise_ns_application_position LESS 0 OR scoped_juce_position LESS 0
   OR NOT initialise_ns_application_position LESS scoped_juce_position)
    message(FATAL_ERROR
        "The macOS test runner must initialise NSApplication before JUCE")
endif()

message(STATUS "Super Bass Fully Agentic Dexed macOS source compatibility verified")
