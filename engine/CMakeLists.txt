# The Anamnesis audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins can reuse this folder (the long memory, the echo, the play heads, the control curves).

add_library(anamnesis_engine STATIC
    src/Engine.cpp
    src/Modifiers.cpp
)

target_include_directories(anamnesis_engine PUBLIC include)
target_compile_features(anamnesis_engine PUBLIC cxx_std_17)

if(ANAMNESIS_BUILD_TESTS)
    # The 0.2.0 engine, unchanged, under another namespace: the self-test proves the new engine still sounds the same
    # with the milestone 3 controls at rest.
    add_library(anamnesis_ref020 STATIC
        tests/ref-0.2.0/ref020.cpp
        tests/ref-0.2.0/src/Engine.cpp
        tests/ref-0.2.0/src/Modifiers.cpp
    )
    target_include_directories(anamnesis_ref020 PRIVATE tests/ref-0.2.0/include PUBLIC tests/ref-0.2.0)
    target_compile_definitions(anamnesis_ref020 PRIVATE anm=anm020)
    target_compile_features(anamnesis_ref020 PUBLIC cxx_std_17)

    add_executable(anamnesis_engine_test tests/engine_test.cpp)
    target_link_libraries(anamnesis_engine_test PRIVATE anamnesis_engine anamnesis_ref020)
    add_test(NAME engine COMMAND anamnesis_engine_test)
endif()
