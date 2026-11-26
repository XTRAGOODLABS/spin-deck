#pragma once

// AI-written (Claude). Review before relying on it.
// Reads Mixxx ".midi.xml" controller mapping files, the same files the original Spin Deck used.
// AI-written (Claude). End.

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace spindeck::mixxx {

enum Option : uint32_t {
    invert = 1u << 0, rot64 = 1u << 1, rot64Invert = 1u << 2, rot64Fast = 1u << 3, diff = 1u << 4,
    button = 1u << 5, switchMode = 1u << 6, hercJog = 1u << 7, hercJogFast = 1u << 8, spread64 = 1u << 9,
    selectKnob = 1u << 10, softTakeover = 1u << 11, script = 1u << 12, fourteenBitMsb = 1u << 13,
    fourteenBitLsb = 1u << 14,
};

struct MappingControl {
    int status = 0, midino = 0;
    juce::String group, key;
    uint32_t options = 0;
};

struct MappingOutput {
    int status = 0, midino = 0, on = 0x7f, off = 0;
    double minimum = 0, maximum = 1;
    juce::String group, key;
};

struct ScriptFile {
    juce::File file;
    juce::String functionPrefix;
};

struct Mapping {
    juce::File file;
    juce::String name, author, controllerId;
    std::vector<ScriptFile> scripts; // common-controller-scripts.js first, as in Mixxx
    std::vector<MappingControl> controls;
    std::vector<MappingOutput> outputs;
    std::vector<juce::String> missingScripts;
    juce::NamedValueSet settings; // <settings> defaults, read by engine.getSetting()
};

struct MappingInfo {
    juce::File file;
    juce::String name, controllerId;
};

// Folders searched for mappings and their script files, most specific first.
juce::Array<juce::File> mappingFolders();
std::vector<MappingInfo> listMappings();
std::optional<Mapping> loadMapping(const juce::File& xmlFile, juce::String* error = nullptr);
// A score above 0 when a mapping looks made for this MIDI device name.
int matchScore(const MappingInfo&, const juce::String& deviceName);

}
