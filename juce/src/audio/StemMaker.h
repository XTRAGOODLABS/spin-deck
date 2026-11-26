#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace spindeck {

class StemMaker final {
  public:
    using Progress = std::function<void(juce::String)>;
    class Cancellation {
      public:
        bool request();
        bool requested() const;

      private:
        friend class StemMaker;
        enum class State { running, cancelled, publishing };
        std::atomic<State> state{State::running};
    };

    static bool isReady();
    static juce::File associatedOutputFor(const juce::File&);
    static juce::File nextOutputFor(const juce::File& source);
    static juce::Result make(const juce::File& source, const juce::File& resources,
            bool allowSetup, juce::File& output, const Progress& progress, Cancellation* cancellation = nullptr,
            const juce::File& destination = {});
};

}
