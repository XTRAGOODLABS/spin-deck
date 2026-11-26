// AI-written (Claude). Review before relying on it.
// Relative-mode timecode: the record's speed and direction drive the deck, as in Mixxx's relative mode.
// AI-written (Claude). End.

#include "audio/TimecodeInput.h"

#include "timecoder.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>

namespace spindeck {

const std::vector<TimecodeInput::Type>& TimecodeInput::types() {
    static const std::vector<Type> list{
            {"serato_2a", "Serato CV02 vinyl, side A"},
            {"serato_2b", "Serato CV02 vinyl, side B"},
            {"serato_cd", "Serato CV02 CD"},
            {"traktor_a", "Traktor Scratch MK1, side A"},
            {"traktor_b", "Traktor Scratch MK1, side B"},
            {"traktor_mk2_a", "Traktor Scratch MK2, side A"},
            {"traktor_mk2_b", "Traktor Scratch MK2, side B"},
            {"traktor_mk2_cd", "Traktor Scratch MK2 CD"},
            {"mixvibes_v2", "MixVibes DVS V2"},
            {"mixvibes_7inch", "MixVibes 7 inch"},
            {"pioneer_a", "Pioneer rekordbox DVS, side A"},
            {"pioneer_b", "Pioneer rekordbox DVS, side B"},
            {"algoriddim_a", "algoriddim djay, side A"},
            {"algoriddim_b", "algoriddim djay, side B"},
    };
    return list;
}

timecode_def* TimecodeInput::definition(const juce::String& id) {
    static std::mutex lock; // xwax keeps its lookup tables in shared state
    const std::lock_guard guard(lock);
    const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("Application Support/Spin Deck JUCE/timecode");
    folder.createDirectory();
    return timecoder_find_definition(id.toRawUTF8(), folder.getFullPathName().toRawUTF8());
}

TimecodeInput::TimecodeInput() = default;

TimecodeInput::~TimecodeInput() {
    if (coder) timecoder_clear(coder.get());
}

void TimecodeInput::prepare(timecode_def* def, double sampleRate) {
    if (coder) {
        timecoder_clear(coder.get());
        coder.reset();
    }
    if (def == nullptr || sampleRate <= 0) return;
    coder = std::make_unique<timecoder>();
    timecoder_init(coder.get(), def, 1.0, static_cast<unsigned int>(sampleRate), false, false);
    work.assign(8192 * 2, 0);
    smoothedSpeed = 0;
    recent.fill(0);
    signal = false;
}

void TimecodeInput::process(const float* left, const float* right, int frames) {
    if (!coder || left == nullptr || right == nullptr || frames <= 0) return;
    double level = 0;
    for (int done = 0; done < frames;) {
        const int count = std::min(frames - done, static_cast<int>(work.size() / 2));
        for (int i = 0; i < count; ++i) {
            const float l = left[done + i], r = right[done + i];
            level += std::abs(l) + std::abs(r);
            work[static_cast<std::size_t>(i) * 2] = static_cast<short>(std::clamp(l * 32767.0f, -32767.0f, 32767.0f));
            work[static_cast<std::size_t>(i) * 2 + 1] = static_cast<short>(std::clamp(r * 32767.0f, -32767.0f, 32767.0f));
        }
        timecoder_submit(coder.get(), work.data(), static_cast<size_t>(count));
        done += count;
    }
    level /= frames;
    // Mixxx treats anything under about 75/32768 as "no needle on the record".
    const bool loud = level > 75.0 / 32768.0;
    quietBlocks = loud ? 0 : quietBlocks + 1;
    signal = loud || quietBlocks < 4;
    const double pitch = signal ? timecoder_get_pitch(coder.get()) : 0.0;
    recent[recentIndex] = pitch;
    recentIndex = (recentIndex + 1) % recent.size();
    const double average = std::accumulate(recent.begin(), recent.end(), 0.0) / static_cast<double>(recent.size());
    // Steady playing: use the average so the music does not wobble. Scratching: follow the hand straight away.
    smoothedSpeed = std::abs(pitch - average) < 0.03 ? average : pitch;
    if (std::abs(smoothedSpeed) < 0.02) smoothedSpeed = 0;
}

}
