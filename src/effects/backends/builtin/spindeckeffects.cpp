#include "effects/backends/builtin/spindeckeffects.h"

#include <cmath>

#include "util/math.h"
#include "util/sample.h"

namespace spindeck {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Length of `beats` beats in frames, or of `beats` seconds when the track has no tempo.
double beatsToFrames(double beats,
        const GroupFeatureState& groupFeatures,
        const mixxx::EngineParameters& p) {
    const double seconds = groupFeatures.beat_length.has_value()
            ? beats * groupFeatures.beat_length->seconds
            : beats;
    return seconds * p.sampleRate();
}

// Round to the nearest musical length: 1/16, 1/8, 1/4, 1/2, 1, 2, 4 ... beats.
double nearestPowerOfTwo(double beats) {
    if (beats <= 0) {
        return 1.0 / 16;
    }
    return std::pow(2.0, std::round(std::log2(beats)));
}

EffectManifestPointer newManifest(const QString& id,
        const QString& name,
        const QString& shortName,
        const QString& description) {
    EffectManifestPointer pManifest(new EffectManifest());
    pManifest->setId(id);
    pManifest->setName(name);
    pManifest->setShortName(shortName);
    pManifest->setAuthor(QStringLiteral("XTRA GOOD LABS"));
    pManifest->setVersion(QStringLiteral("1.0"));
    pManifest->setDescription(description);
    // The engine crossfades from the dry sound when the effect turns on and back
    // to it when it turns off, so there are no clicks.
    pManifest->setAddDryToWet(false);
    pManifest->setEffectRampsFromDry(false);
    return pManifest;
}

EffectManifestParameterPointer addKnob(const EffectManifestPointer& pManifest,
        const QString& id,
        const QString& name,
        const QString& description,
        double min,
        double def,
        double max,
        EffectManifestParameter::UnitsHint units =
                EffectManifestParameter::UnitsHint::Unknown) {
    EffectManifestParameterPointer pParam = pManifest->addParameter();
    pParam->setId(id);
    pParam->setName(name);
    pParam->setShortName(name);
    pParam->setDescription(description);
    pParam->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    pParam->setUnitsHint(units);
    pParam->setRange(min, def, max);
    return pParam;
}

// Fast white noise in -1..1.
inline CSAMPLE noise(quint32* pSeed) {
    *pSeed = *pSeed * 1664525u + 1013904223u;
    return static_cast<CSAMPLE>(static_cast<qint32>(*pSeed)) / 2147483648.0f;
}

} // namespace

void Biquad::setBandPass(double hz, double q, double sampleRate) {
    const double w = 2 * kPi * hz / sampleRate;
    const double alpha = std::sin(w) / (2 * q);
    const double a0 = 1 + alpha;
    b0 = alpha / a0;
    b1 = 0;
    b2 = -alpha / a0;
    a1 = -2 * std::cos(w) / a0;
    a2 = (1 - alpha) / a0;
}

void Biquad::setHighPass(double hz, double q, double sampleRate) {
    const double w = 2 * kPi * hz / sampleRate;
    const double alpha = std::sin(w) / (2 * q);
    const double cosw = std::cos(w);
    const double a0 = 1 + alpha;
    b0 = (1 + cosw) / 2 / a0;
    b1 = -(1 + cosw) / a0;
    b2 = (1 + cosw) / 2 / a0;
    a1 = -2 * cosw / a0;
    a2 = (1 - alpha) / a0;
}

// ======================================================================= Echo Out
// Turn it on for an echo. Turn the knob up and the song drops away while its echo
// keeps going, so you can leave the track on a tail.

EchoOutState::EchoOutState(const mixxx::EngineParameters& p)
        : EffectState(p) {
    ring.allocate(static_cast<SINT>(p.sampleRate() * 5)); // up to 5 s of echo
}

EchoOutEffect::EchoOutEffect() {
    for (int deck = 1; deck <= 4; ++deck) {
        const QString group = QStringLiteral("[Channel%1]").arg(deck);
        m_deckBpm.emplace_back(group, QStringLiteral("bpm"), ControlFlag::AllowMissingOrInvalid);
        m_deckPlay.emplace_back(group, QStringLiteral("play"), ControlFlag::AllowMissingOrInvalid);
    }
}

QString EchoOutEffect::getId() {
    return QStringLiteral("com.xtragoodlabs.spindeck.echoout");
}

EffectManifestPointer EchoOutEffect::getManifest() {
    auto pManifest = newManifest(getId(),
            QObject::tr("Echo Out"),
            QObject::tr("EchoOut"),
            QObject::tr("A beat-timed echo. Turn the knob up to drop the song and "
                        "leave only its echo, for leaving a track on a tail."));
    addKnob(pManifest,
            QStringLiteral("time"),
            QObject::tr("Time"),
            QObject::tr("Echo time in beats (1/4 to 2), or seconds when there is no tempo"),
            0.25,
            0.75,
            2.0,
            EffectManifestParameter::UnitsHint::Beats);
    addKnob(pManifest,
            QStringLiteral("feedback"),
            QObject::tr("Feedback"),
            QObject::tr("How long the echo keeps repeating"),
            0.0,
            0.65,
            0.92);
    auto out = addKnob(pManifest,
            QStringLiteral("out"),
            QObject::tr("Out"),
            QObject::tr("0: song plus echo. Full: the song is gone and only the echo is left"),
            0.0,
            0.0,
            1.0);
    out->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    pManifest->setMetaknobDefault(0.0);
    return pManifest;
}

void EchoOutEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pTime = parameters.value(QStringLiteral("time"));
    m_pFeedback = parameters.value(QStringLiteral("feedback"));
    m_pOut = parameters.value(QStringLiteral("out"));
}

void EchoOutEffect::processChannel(EchoOutState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& p,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    Ring& ring = pState->ring;
    const SINT frames = p.framesPerBuffer();

    // Time the echo to the INCOMING track: the other playing deck. This deck's own
    // beat is used only when no other deck is playing, or both run at the same tempo.
    const double ownBeat = groupFeatures.beat_length.has_value()
            ? groupFeatures.beat_length->seconds
            : 0.0;
    double beatSeconds = ownBeat;
    double mostDifferent = -1;
    for (size_t d = 0; d < m_deckBpm.size(); ++d) {
        const double bpm = m_deckBpm[d].get();
        if (m_deckPlay[d].get() <= 0 || bpm < 20) {
            continue;
        }
        const double beat = 60.0 / bpm;
        const double diff = ownBeat > 0 ? std::fabs(beat - ownBeat) / ownBeat : 1.0;
        if (diff > mostDifferent) {
            mostDifferent = diff;
            beatSeconds = beat;
        }
    }
    if (ownBeat > 0 && mostDifferent >= 0 && mostDifferent < 0.002) {
        beatSeconds = ownBeat; // same tempo either way
    }
    const double seconds = beatSeconds > 0 ? m_pTime->value() * beatSeconds
                                           : m_pTime->value();
    const double target = math_clamp(seconds * p.sampleRate(),
            1.0,
            static_cast<double>(ring.frames - 2));
    if (pState->prevDelay <= 0 || enableState == EffectEnableState::Enabling) {
        pState->prevDelay = target;
    }
    // A big tempo change glides over about a quarter second instead of jumping.
    const double maxGlide = pState->prevDelay * 0.08;
    const double newDelay = pState->prevDelay +
            math_clamp(target - pState->prevDelay, -maxGlide, maxGlide);
    const SINT delay = static_cast<SINT>(newDelay);

    if (enableState == EffectEnableState::Enabling) {
        pState->prevOut = 0;
    }
    // The song never drops faster than over one echo, so pressing the FX button
    // with the knob already up still leaves a tail.
    const double maxStep = static_cast<double>(frames) / delay;
    const double out = std::min(m_pOut->value(), pState->prevOut + maxStep);
    const double feedback = m_pFeedback->value();
    for (SINT i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i + 1) / frames;
        const double o = pState->prevOut + (out - pState->prevOut) * t;
        const double fb = pState->prevFeedback + (feedback - pState->prevFeedback) * t;
        // The song keeps feeding the echo until the knob is nearly all the way up.
        const double feed = 1.0 - math_clamp((o - 0.6) / 0.4, 0.0, 1.0);
        const double dl = pState->prevDelay + (newDelay - pState->prevDelay) * t;
        double pos = ring.write - dl;
        if (pos < 0) {
            pos += ring.frames;
        }
        const SINT a = static_cast<SINT>(pos) % ring.frames;
        const SINT b = (a + 1) % ring.frames;
        const double frac = pos - std::floor(pos);
        for (int ch = 0; ch < 2; ++ch) {
            const CSAMPLE x = pInput[i * 2 + ch];
            const CSAMPLE echo = static_cast<CSAMPLE>(
                    ring.data[a * 2 + ch] * (1 - frac) + ring.data[b * 2 + ch] * frac);
            pOutput[i * 2 + ch] = static_cast<CSAMPLE>(x * (1.0 - o)) + echo;
            // Soft limit the loop so high feedback never runs away.
            const double fed = x * feed + echo * fb;
            ring.data[ring.write * 2 + ch] = static_cast<CSAMPLE>(std::tanh(fed));
        }
        ring.write = (ring.write + 1) % ring.frames;
    }
    pState->prevOut = out;
    pState->prevFeedback = feedback;
    pState->prevDelay = newDelay;
    if (enableState == EffectEnableState::Disabling) {
        ring.clear();
    }
}

// ======================================================================= Riser
// Builds tension: a sweep of noise that rises with the knob while the bass of the
// song thins out underneath.

QString RiserEffect::getId() {
    return QStringLiteral("com.xtragoodlabs.spindeck.riser");
}

EffectManifestPointer RiserEffect::getManifest() {
    auto pManifest = newManifest(getId(),
            QObject::tr("Riser"),
            QObject::tr("Riser"),
            QObject::tr("Builds tension: turn the knob up for a rising noise sweep "
                        "while the song's bass thins out."));
    auto build = addKnob(pManifest,
            QStringLiteral("build"),
            QObject::tr("Build"),
            QObject::tr("How far the riser has climbed"),
            0.0,
            0.0,
            1.0);
    build->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    addKnob(pManifest,
            QStringLiteral("noise"),
            QObject::tr("Noise"),
            QObject::tr("How loud the noise sweep is"),
            0.0,
            0.5,
            1.0);
    pManifest->setMetaknobDefault(0.0);
    return pManifest;
}

void RiserEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pBuild = parameters.value(QStringLiteral("build"));
    m_pNoise = parameters.value(QStringLiteral("noise"));
}

void RiserEffect::processChannel(RiserState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& p,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    Q_UNUSED(groupFeatures);
    const SINT frames = p.framesPerBuffer();
    const double build = m_pBuild->value();
    const double noiseLevel = m_pNoise->value() * 0.35;
    const double nyquistSafe = p.sampleRate() * 0.45;

    // Filters are set once per buffer at the middle of the move.
    const double b = (pState->prevBuild + build) / 2;
    pState->noiseFilter.setBandPass(std::min(nyquistSafe, 250.0 * std::pow(2.0, b * 5.5)),
            1.2,
            p.sampleRate());
    pState->musicFilter.setHighPass(std::min(nyquistSafe, 20.0 + 900.0 * b * b),
            0.707,
            p.sampleRate());

    for (SINT i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i + 1) / frames;
        const double bb = pState->prevBuild + (build - pState->prevBuild) * t;
        const CSAMPLE n = noise(&pState->seed);
        for (int ch = 0; ch < 2; ++ch) {
            const CSAMPLE music = pState->musicFilter.run(ch, pInput[i * 2 + ch]);
            const CSAMPLE sweep = pState->noiseFilter.run(ch, n);
            pOutput[i * 2 + ch] = music + static_cast<CSAMPLE>(sweep * noiseLevel * bb * 3.0);
        }
    }
    pState->prevBuild = build;
    if (enableState == EffectEnableState::Disabling) {
        pState->noiseFilter.reset();
        pState->musicFilter.reset();
    }
}

// ======================================================================= Roll
// Grabs a slice of the beat the moment it is turned on and repeats it, like a
// beat roll. The knob picks the slice: 1/16 to 1 beat.

RollState::RollState(const mixxx::EngineParameters& p)
        : EffectState(p) {
    loop.allocate(static_cast<SINT>(p.sampleRate() * 4));
}

QString RollEffect::getId() {
    return QStringLiteral("com.xtragoodlabs.spindeck.roll");
}

EffectManifestPointer RollEffect::getManifest() {
    auto pManifest = newManifest(getId(),
            QObject::tr("Roll"),
            QObject::tr("Roll"),
            QObject::tr("Repeats a slice of the beat from the moment it is turned on. "
                        "The knob picks the slice, from 1/16 to 1 beat."));
    auto length = addKnob(pManifest,
            QStringLiteral("length"),
            QObject::tr("Length"),
            QObject::tr("Slice length: 1/16, 1/8, 1/4, 1/2 or 1 beat"),
            1.0 / 16,
            0.25,
            1.0,
            EffectManifestParameter::UnitsHint::Beats);
    length->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    pManifest->setMetaknobDefault(0.5);
    return pManifest;
}

void RollEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pLength = parameters.value(QStringLiteral("length"));
}

void RollEffect::processChannel(RollState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& p,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    const SINT frames = p.framesPerBuffer();
    if (enableState == EffectEnableState::Enabling || !pState->active) {
        pState->active = true;
        pState->captured = 0;
        pState->play = 0;
    }
    const double beats = nearestPowerOfTwo(m_pLength->value());
    SINT length = static_cast<SINT>(beatsToFrames(beats, groupFeatures, p));
    length = math_clamp<SINT>(length, 64, pState->loop.frames);
    if (length != pState->length && pState->captured >= pState->length) {
        // A new slice size: keep the start of what was grabbed, grab more if needed.
        pState->play = pState->play % length;
    }
    pState->length = length;

    for (SINT i = 0; i < frames; ++i) {
        if (pState->captured < pState->length) {
            // Still grabbing the slice: let the song through while it records.
            for (int ch = 0; ch < 2; ++ch) {
                const CSAMPLE x = pInput[i * 2 + ch];
                pState->loop.data[pState->captured * 2 + ch] = x;
                pOutput[i * 2 + ch] = x;
            }
            ++pState->captured;
            pState->play = pState->captured % pState->length;
        } else {
            // Short fades at the slice edges so the repeat does not click.
            const SINT edge = std::min<SINT>(64, pState->length / 4);
            double g = 1.0;
            if (pState->play < edge) {
                g = static_cast<double>(pState->play) / edge;
            } else if (pState->play > pState->length - edge) {
                g = static_cast<double>(pState->length - pState->play) / edge;
            }
            for (int ch = 0; ch < 2; ++ch) {
                pOutput[i * 2 + ch] = static_cast<CSAMPLE>(
                        pState->loop.data[pState->play * 2 + ch] * g);
            }
            pState->play = (pState->play + 1) % pState->length;
        }
    }
    if (enableState == EffectEnableState::Disabling) {
        pState->active = false;
    }
}

// ======================================================================= Brake
// A turntable stop: the song slows down to a halt over the chosen time, then stays
// silent until the effect is turned off.

BrakeState::BrakeState(const mixxx::EngineParameters& p)
        : EffectState(p) {
    ring.allocate(static_cast<SINT>(p.sampleRate() * 10));
}

QString BrakeEffect::getId() {
    return QStringLiteral("com.xtragoodlabs.spindeck.brake");
}

EffectManifestPointer BrakeEffect::getManifest() {
    auto pManifest = newManifest(getId(),
            QObject::tr("Brake"),
            QObject::tr("Brake"),
            QObject::tr("A turntable stop: the song slows to a halt. The knob sets "
                        "how long it takes, from 1/2 beat to 8 beats."));
    auto time = addKnob(pManifest,
            QStringLiteral("time"),
            QObject::tr("Time"),
            QObject::tr("How long the stop takes, in beats (seconds with no tempo)"),
            0.5,
            2.0,
            8.0,
            EffectManifestParameter::UnitsHint::Beats);
    time->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    pManifest->setMetaknobDefault(0.2);
    return pManifest;
}

void BrakeEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pTime = parameters.value(QStringLiteral("time"));
}

void BrakeEffect::processChannel(BrakeState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& p,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    Ring& ring = pState->ring;
    const SINT frames = p.framesPerBuffer();
    if (enableState == EffectEnableState::Enabling || !pState->active) {
        pState->active = true;
        pState->speed = 1.0;
        pState->readPos = 0.0;
    }
    const double stopFrames = std::max(1.0, beatsToFrames(m_pTime->value(), groupFeatures, p));
    const double step = 1.0 / stopFrames; // speed lost per frame

    for (SINT i = 0; i < frames; ++i) {
        // Keep recording the song so the stop plays what was just heard.
        for (int ch = 0; ch < 2; ++ch) {
            ring.data[ring.write * 2 + ch] = pInput[i * 2 + ch];
        }
        ring.write = (ring.write + 1) % ring.frames;

        // readPos = how far behind live the playhead is. It grows by (1 - speed).
        pState->readPos = std::min(pState->readPos + (1.0 - pState->speed),
                static_cast<double>(ring.frames - 2));
        const double pos = ring.write - 1 - pState->readPos;
        const double wrapped = pos < 0 ? pos + ring.frames : pos;
        const SINT a = static_cast<SINT>(wrapped) % ring.frames;
        const SINT b = (a + 1) % ring.frames;
        const double frac = wrapped - std::floor(wrapped);
        // Fade the last bit so the stop ends in silence, not a buzz.
        const double gain = std::min(1.0, pState->speed * 6.0);
        for (int ch = 0; ch < 2; ++ch) {
            const double s = ring.data[a * 2 + ch] * (1 - frac) + ring.data[b * 2 + ch] * frac;
            pOutput[i * 2 + ch] = static_cast<CSAMPLE>(s * gain);
        }
        pState->speed = std::max(0.0, pState->speed - step);
    }
    if (enableState == EffectEnableState::Disabling) {
        pState->active = false;
        ring.clear();
    }
}

} // namespace spindeck
