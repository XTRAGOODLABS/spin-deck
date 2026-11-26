#pragma once

#include <JuceHeader.h>

namespace spindeck {

class StemQueueWindow final : public juce::DocumentWindow {
  public:
    explicit StemQueueWindow(juce::Component* owner);
    void pauseForQuit(std::function<void()> onPaused);
    // A short line for the main window corner, empty when nothing is being split.
    juce::String progressNote() const;
    void closeButtonPressed() override;

  private:
    class Queue;
};

}
