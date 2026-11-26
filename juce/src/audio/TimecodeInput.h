#pragma once

// AI-written (Claude). Review before relying on it.
// Timecode vinyl / DVS input (for example an MWM Phase receiver set to Serato or Traktor).
// The decoding is xwax's timecoder (GPLv3, Mark Hills), the same code Mixxx uses.
// AI-written (Claude). End.

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

struct timecoder;
struct timecode_def;

namespace spindeck {

class TimecodeInput {
  public:
    struct Type { const char* id; const char* label; };
    static const std::vector<Type>& types();
    // Builds the lookup table for a timecode type. Can take a moment the first time; call off the audio thread.
    static timecode_def* definition(const juce::String& id);

    TimecodeInput();
    ~TimecodeInput();

    void prepare(timecode_def* def, double sampleRate);
    bool isReady() const { return coder != nullptr; }
    // Audio thread. left and right are the two input channels the turntable or Phase receiver uses.
    void process(const float* left, const float* right, int frames);

    // Speed of the record: 1 is normal, 0 still, negative is backwards.
    double speed() const { return smoothedSpeed; }
    bool hasSignal() const { return signal; }

  private:
    std::unique_ptr<timecoder> coder;
    std::vector<short> work;
    double smoothedSpeed = 0;
    std::array<double, 12> recent{};
    std::size_t recentIndex = 0;
    bool signal = false;
    int quietBlocks = 0;
};

}
