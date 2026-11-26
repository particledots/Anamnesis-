# The Anamnesis plugin wrapper: what Logic sees (AU / VST3), the controls and the panel.
# The sound itself lives in ../engine.
#
# The identifiers below were chosen once, for the first build, and must never change
# (Logic finds saved projects' plugins by them).

juce_add_plugin(Anamnesis
    COMPANY_NAME                "particledots"
    BUNDLE_ID                   com.particledots.anamnesis
    PLUGIN_MANUFACTURER_CODE    Pdot
    PLUGIN_CODE                 Anms
    FORMATS                     AU VST3
    PRODUCT_NAME                "Anamnesis"
    PLUGIN_NAME                 "Anamnesis"
    DESCRIPTION                 "Delay with a 3-minute memory (inspired by the Chase Bliss Habit; not affiliated)"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Delay
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(Anamnesis PRIVATE
    Source/Parameters.h
    Source/Parameters.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/Controls.h
    Source/Controls.cpp
    Source/Style.h
)

target_compile_definitions(Anamnesis PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Anamnesis
    PRIVATE
        anamnesis_engine
        AnamnesisFonts
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
