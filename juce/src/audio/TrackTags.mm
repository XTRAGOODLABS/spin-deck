#include "audio/TrackTags.h"
#include "audio/StemFile.h"

#import <AVFoundation/AVFoundation.h>

namespace spindeck {
namespace {

juce::String toJuce(NSString* value) {
    return value ? juce::String::fromUTF8(value.UTF8String).trim() : juce::String{};
}

juce::String stringValue(AVMetadataItem* item) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (NSString* text = item.stringValue) {
        return toJuce(text);
    }
    if (NSNumber* number = item.numberValue) {
        return toJuce(number.stringValue);
    }
#pragma clang diagnostic pop
    return {};
}

// ID3 and iTunes store genre as a number in a few older files.
juce::String genreName(juce::String value) {
    static const juce::StringArray names{"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
            "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock",
            "Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno",
            "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid",
            "House"};
    value = value.trim();
    if (value.startsWithChar('(') && value.containsChar(')')) {
        value = value.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false);
    }
    if (value.containsOnly("0123456789") && value.isNotEmpty()) {
        const int index = value.getIntValue();
        return juce::isPositiveAndBelow(index, names.size()) ? names[index] : juce::String{};
    }
    return value;
}

AVURLAsset* openAsset(const juce::File& file) {
    NSString* path = [NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()];
    return [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path] options:nil];
}

}

std::optional<TrackTags> readTrackTags(const juce::File& file) {
    @autoreleasepool {
        if (!file.existsAsFile()) {
            return {};
        }
        auto* asset = openAsset(file);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSArray<AVMetadataItem*>* common = asset.commonMetadata;
        NSArray<AVMetadataItem*>* all = asset.metadata;
#pragma clang diagnostic pop
        TrackTags tags;
        for (AVMetadataItem* item in common) {
            NSString* key = item.commonKey;
            if ([key isEqualToString:AVMetadataCommonKeyTitle]) {
                tags.title = stringValue(item);
            } else if ([key isEqualToString:AVMetadataCommonKeyArtist]) {
                tags.artist = stringValue(item);
            } else if ([key isEqualToString:AVMetadataCommonKeyAlbumName]) {
                tags.album = stringValue(item);
            } else if ([key isEqualToString:AVMetadataCommonKeyCreationDate] && tags.year.isEmpty()) {
                tags.year = stringValue(item).substring(0, 4);
            }
        }
        for (AVMetadataItem* item in all) {
            NSString* identifier = item.identifier;
            if (tags.genre.isEmpty() &&
                    ([identifier isEqualToString:AVMetadataIdentifierID3MetadataContentType] ||
                            [identifier isEqualToString:AVMetadataIdentifieriTunesMetadataUserGenre] ||
                            [identifier isEqualToString:AVMetadataIdentifieriTunesMetadataPredefinedGenre] ||
                            [identifier isEqualToString:AVMetadataIdentifierQuickTimeMetadataGenre])) {
                tags.genre = genreName(stringValue(item));
            }
            if (tags.year.isEmpty() &&
                    ([identifier isEqualToString:AVMetadataIdentifierID3MetadataYear] ||
                            [identifier isEqualToString:AVMetadataIdentifierID3MetadataRecordingTime] ||
                            [identifier isEqualToString:AVMetadataIdentifieriTunesMetadataReleaseDate])) {
                tags.year = stringValue(item).substring(0, 4);
            }
        }
        if (!tags.year.containsOnly("0123456789")) {
            tags.year = {};
        }
        return tags;
    }
}

namespace {
juce::Image embeddedArtwork(const juce::File& file);

// Stem copies made by Make Stems carry the song's tags but not its cover, so look for the original
// song beside it ("Song.stem.mp4" next to "Song.mp3", "Song.m4a" and so on).
juce::File originalForStem(const juce::File& stem) {
    if (!isStemFile(stem)) {
        return {};
    }
    const auto base = stem.getFileNameWithoutExtension().upToLastOccurrenceOf(".stem", false, true);
    for (const auto* extension : {".mp3", ".m4a", ".aiff", ".aif", ".flac", ".wav", ".mp4", ".alac", ".aac"}) {
        const auto candidate = stem.getSiblingFile(base + extension);
        if (candidate.existsAsFile()) {
            return candidate;
        }
    }
    return {};
}
}

juce::Image readTrackArtwork(const juce::File& file) {
    auto image = embeddedArtwork(file);
    if (!image.isValid()) {
        if (const auto original = originalForStem(file); original != juce::File{}) {
            image = embeddedArtwork(original);
        }
    }
    return image;
}

namespace {
juce::Image embeddedArtwork(const juce::File& file) {
    @autoreleasepool {
        if (!file.existsAsFile()) {
            return {};
        }
        auto* asset = openAsset(file);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSArray<AVMetadataItem*>* common = asset.commonMetadata;
#pragma clang diagnostic pop
        for (AVMetadataItem* item in common) {
            if (![item.commonKey isEqualToString:AVMetadataCommonKeyArtwork]) {
                continue;
            }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSData* data = item.dataValue;
            if (!data && [item.value isKindOfClass:[NSDictionary class]]) {
                data = ((NSDictionary*) item.value)[@"data"];
            }
#pragma clang diagnostic pop
            if (data.length > 0) {
                auto image = juce::ImageFileFormat::loadFrom(data.bytes, static_cast<std::size_t>(data.length));
                if (image.isValid()) {
                    // Decks show a small square, so keep memory low for large covers.
                    const int edge = 300;
                    if (image.getWidth() > edge || image.getHeight() > edge) {
                        image = image.rescaled(edge, edge, juce::Graphics::highResamplingQuality);
                    }
                    return image;
                }
            }
        }
        return {};
    }
}

}

}
