#include "audio/PluginHostService.h"

namespace spindeck {

PluginHostService::PluginHostService() {
    juce::addDefaultFormatsToManager(formats);
}

juce::StringArray PluginHostService::availableFormatNames() const {
    juce::StringArray names;
    for (int i = 0; i < formats.getNumFormats(); ++i) {
        names.add(formats.getFormat(i)->getName());
    }
    return names;
}

juce::AudioPluginFormatManager& PluginHostService::formatManager() {
    return formats;
}

}
