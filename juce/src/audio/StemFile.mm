#include "audio/StemFile.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <cmath>
#include <vector>

namespace spindeck {

bool isStemFile(const juce::File& file) {
    const auto name = file.getFileName();
    return name.endsWithIgnoreCase(".stem.mp4") || name.endsWithIgnoreCase(".stem.m4a");
}

bool readStemFile(const juce::File& file, StemFile& decoded, double mixExcerptSeconds) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()];
        auto* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path] options:nil];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        auto* tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
        const CMTimeRange mixRange = tracks.count == 5 ? [tracks[0] timeRange] : kCMTimeRangeInvalid;
#pragma clang diagnostic pop
        const double fullDuration = CMTimeGetSeconds(mixRange.duration);
        const bool excerpt = mixExcerptSeconds > 0;
        const double duration = excerpt ? std::min(fullDuration, mixExcerptSeconds) : fullDuration;
        constexpr int rate = stemDecodeRate;
        constexpr int maxFrames = maxStemDecodeFrames;
        if (!CMTIMERANGE_IS_VALID(mixRange) || !std::isfinite(duration) ||
                duration <= 0 || duration * rate > maxFrames) {
            return false;
        }
        const int frames = static_cast<int>(std::llround(duration * rate));
        if (frames <= 0) {
            return false;
        }
        NSDictionary* settings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVSampleRateKey: @(rate),
            AVNumberOfChannelsKey: @2,
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey: @YES,
            AVLinearPCMIsBigEndianKey: @NO,
            AVLinearPCMIsNonInterleaved: @NO,
        };
        auto* reader = [[AVAssetReader alloc] initWithAsset:asset error:nil];
        if (!reader) {
            return false;
        }
        if (excerpt && duration < fullDuration) {
            reader.timeRange = CMTimeRangeMake(mixRange.start, CMTimeMake(frames, rate));
        }
        std::array<AVAssetReaderTrackOutput*, 5> outputs{};
        const std::size_t outputCount = excerpt ? 1 : outputs.size();
        for (std::size_t i = 0; i < outputCount; ++i) {
            auto* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[i]
                    outputSettings:settings];
            if (![reader canAddOutput:output]) {
                return false;
            }
            [reader addOutput:output];
            outputs[i] = output;
        }
        if (![reader startReading]) {
            return false;
        }
        std::array<int, 5> received{};
        std::vector<float> interleaved;
        for (std::size_t i = 0; i < outputCount; ++i) {
            auto& destination = i == 0 ? decoded.mix : decoded.parts[i - 1];
            destination.setSize(2, frames);
            destination.clear();
            auto* left = destination.getWritePointer(0);
            auto* right = destination.getWritePointer(1);
            while (auto* sample = [outputs[i] copyNextSampleBuffer]) {
                const auto timestamp = CMTimeSubtract(CMSampleBufferGetPresentationTimeStamp(sample),
                        mixRange.start);
                const double time = CMTimeGetSeconds(timestamp);
                const auto count = CMSampleBufferGetNumSamples(sample);
                auto* data = CMSampleBufferGetDataBuffer(sample);
                const auto byteCount = count * 2 * sizeof(float);
                bool copied = false;
                if (std::isfinite(time) && std::abs(time) < duration + 1 && data &&
                        count > 0 && count <= maxFrames &&
                        CMBlockBufferGetDataLength(data) == byteCount) {
                    interleaved.resize(static_cast<std::size_t>(count) * 2);
                    copied = CMBlockBufferCopyDataBytes(data, 0, byteCount, interleaved.data()) == noErr;
                }
                CFRelease(sample);
                if (!copied) {
                    return false;
                }
                const int first = static_cast<int>(std::llround(time * rate));
                const int from = std::max(0, -first);
                const int to = std::min(static_cast<int>(count), frames - first);
                for (int index = from; index < to; ++index) {
                    left[first + index] = interleaved[2 * index];
                    right[first + index] = interleaved[2 * index + 1];
                }
                received[i] += std::max(0, to - from);
            }
        }
        if (reader.status != AVAssetReaderStatusCompleted ||
                std::any_of(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(outputCount), [](int count) { return count == 0; })) {
            return false;
        }
        decoded.rate = rate;
        return true;
    }
}

}
