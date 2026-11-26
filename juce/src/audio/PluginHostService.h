#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace spindeck {

class PluginHostService {
  public:
    PluginHostService();
    juce::StringArray availableFormatNames() const;
    juce::AudioPluginFormatManager& formatManager();

  private:
    juce::AudioPluginFormatManager formats;
};

}
