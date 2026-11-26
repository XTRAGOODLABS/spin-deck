#pragma once

#include <JuceHeader.h>

namespace spindeck {

class StemQueueWindow final : public juce::DocumentWindow {
  public:
    explicit StemQueueWindow(juce::Component* owner);
    void pauseForQuit(std::function<void()> onPaused);
    void closeButtonPressed() override;

  private:
    class Queue;
};

}
