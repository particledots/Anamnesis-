# The Anamnesis audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins can reuse this folder (the long memory, the echo, the play heads, the control curves).

add_library(anamnesis_engine STATIC
    src/Engine.cpp
    src/Modifiers.cpp
)

target_include_directories(anamnesis_engine PUBLIC include)
target_compile_features(anamnesis_engine PUBLIC cxx_std_17)

if(ANAMNESIS_BUILD_TESTS)
    add_executable(anamnesis_engine_test tests/engine_test.cpp)
    target_link_libraries(anamnesis_engine_test PRIVATE anamnesis_engine)
    add_test(NAME engine COMMAND anamnesis_engine_test)
endif()
