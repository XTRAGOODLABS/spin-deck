#pragma once

// Spin Deck DJ effects: Echo Out, Riser, Roll and Brake.
// Each one is made for the moment a DJ hits an FX button: the main move sits on
// the effect's meta knob, and timing follows the track's beats when it has them.

#include <QMap>
#include <vector>

#include "effects/backends/effectprocessor.h"
#include "engine/effects/engineeffect.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/class.h"
#include "util/types.h"

namespace spindeck {

/// A stereo ring buffer of past audio.
struct Ring {
    std::vector<CSAMPLE> data; // interleaved L/R
    SINT frames = 0;
    SINT write = 0;            // next frame to write

    void allocate(SINT frameCount) {
        frames = frameCount;
        data.assign(static_cast<size_t>(frameCount) * 2, 0);
        write = 0;
    }
    void clear() {
        std::fill(data.begin(), data.end(), 0);
        write = 0;
    }
};

/// A simple two-pole filter (RBJ biquad), one per stereo channel.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0};
    double z2[2] = {0, 0};
    void setBandPass(double hz, double q, double sampleRate);
    void setHighPass(double hz, double q, double sampleRate);
    CSAMPLE run(int ch, CSAMPLE x) {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return static_cast<CSAMPLE>(y);
    }
    void reset() {
        z1[0] = z1[1] = z2[0] = z2[1] = 0;
    }
};

// ---------------------------------------------------------------- Echo Out

class EchoOutState : public EffectState {
  public:
    explicit EchoOutState(const mixxx::EngineParameters& p);
    Ring ring;
    double prevOut = 0;
    double prevFeedback = 0;
};

class EchoOutEffect : public EffectProcessorImpl<EchoOutState> {
  public:
    EchoOutEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(EchoOutState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    EngineEffectParameterPointer m_pTime;
    EngineEffectParameterPointer m_pFeedback;
    EngineEffectParameterPointer m_pOut;
    DISALLOW_COPY_AND_ASSIGN(EchoOutEffect);
};

// ---------------------------------------------------------------- Riser

class RiserState : public EffectState {
  public:
    explicit RiserState(const mixxx::EngineParameters& p)
            : EffectState(p) {
    }
    Biquad noiseFilter;
    Biquad musicFilter;
    double prevBuild = 0;
    quint32 seed = 22222;
};

class RiserEffect : public EffectProcessorImpl<RiserState> {
  public:
    RiserEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(RiserState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    EngineEffectParameterPointer m_pBuild;
    EngineEffectParameterPointer m_pNoise;
    DISALLOW_COPY_AND_ASSIGN(RiserEffect);
};

// ---------------------------------------------------------------- Roll

class RollState : public EffectState {
  public:
    explicit RollState(const mixxx::EngineParameters& p);
    Ring loop;        // the captured slice
    SINT captured = 0; // frames captured so far
    SINT length = 0;   // frames in the slice
    SINT play = 0;     // playback frame within the slice
    bool active = false;
};

class RollEffect : public EffectProcessorImpl<RollState> {
  public:
    RollEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(RollState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    EngineEffectParameterPointer m_pLength;
    DISALLOW_COPY_AND_ASSIGN(RollEffect);
};

// ---------------------------------------------------------------- Brake

class BrakeState : public EffectState {
  public:
    explicit BrakeState(const mixxx::EngineParameters& p);
    Ring ring;
    double readPos = 0;   // frames behind the write head, fractional
    double speed = 1;
    bool active = false;
};

class BrakeEffect : public EffectProcessorImpl<BrakeState> {
  public:
    BrakeEffect() = default;
    static QString getId();
    static EffectManifestPointer getManifest();
    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;
    void processChannel(BrakeState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    EngineEffectParameterPointer m_pTime;
    DISALLOW_COPY_AND_ASSIGN(BrakeEffect);
};

} // namespace spindeck
