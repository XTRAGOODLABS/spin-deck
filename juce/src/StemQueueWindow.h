#pragma once

#include <JuceHeader.h>

namespace spindeck {

class StemQueueWindow final : public juce::DocumentWindow {
  public:
    explicit StemQueueWindow(juce::Component* owner);
    void pauseForQuit(std::function<void()> onPaused);
    // A short line for the main window corner, empty when nothing is being split.
    juce::String progressNote() const;
    // Queue songs for splitting (from the library's right-click menu). Offers the one-time setup if needed.
    void addTracks(const juce::Array<juce::File>&);
    void cancelCurrent();
    void cancelWaiting();
    int waitingCount() const;
    void closeButtonPressed() override;

  private:
    class Queue;
};

}
