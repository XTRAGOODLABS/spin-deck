#pragma once

#include "audio/TrackMetadata.h"
#include <optional>

namespace spindeck {

class GoodMixxHistory {
  public:
    struct Song {
        juce::File file;
        juce::String name;
        juce::StringArray alternatePaths;

        bool matches(const juce::File&) const;
    };

    struct TempoSetup {
        enum class Leader { outgoing, incoming };
        struct Deck {
            bool sync = false;
            bool keyLock = false;
        };

        std::optional<Leader> leader;
        Deck outgoing, incoming;
    };

    struct Points {
        std::optional<TrackLoop> loop;
        std::optional<double> cue;
        std::optional<TempoSetup> tempo;
    };

    struct Mix {
        Song outgoing, incoming;
        int count;
        juce::int64 lastSavedMs;
        std::optional<Points> points{};
    };

    explicit GoodMixxHistory(juce::File file);
    bool load();
    bool save(const juce::File& outgoing, const juce::File& incoming, Points);
    std::optional<int> relinkMissing(const juce::Array<juce::File>& files);
    bool importProduction(const juce::File& productionDirectory);
    bool hasProductionImport() const;
    const std::vector<Mix>& mixes() const { return entries; }

  private:
    bool write(std::vector<Mix>, int revision);
    juce::String lockName() const;
    juce::File file;
    std::vector<Mix> entries;
    int productionImportRevision = 0;
};

}
