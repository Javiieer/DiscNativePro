#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class YouTubeAudioDownloader
{
public:
    using ProgressCallback = std::function<void(float progress, const juce::String& statusText)>;
    using CompletionCallback = std::function<void(const juce::File& audioFile, bool success, const juce::String& errorMsg)>;

    static YouTubeAudioDownloader& getInstance();

    // Cache directory: ~/Library/Caches/DiscNativePro/YouTubeAudio/
    juce::File getCacheFolder() const;

    // Returns the cached file if already downloaded and valid (> 10KB)
    juce::File getCachedAudioFile(const juce::String& videoId) const;

    // Checks if a specific track is currently being downloaded
    bool isDownloading(const juce::String& videoId) const;

    // Locates the verified app-managed yt-dlp binary or a user-installed binary.
    juce::String getBinaryPath() const;

    // Ensures a platform-specific upstream binary is installed after SHA-256 verification.
    void ensureBinaryInstalledAsync(std::function<void(bool success)> onDone = nullptr);

    // Downloads YouTube audio in background
    // Callbacks are dispatched safely on the JUCE Message Thread
    void downloadAudioAsync(const juce::String& videoId,
                            ProgressCallback onProgress,
                            CompletionCallback onComplete);

    void downloadAudioAsync(const juce::String& videoId,
                            const juce::String& title,
                            const juce::String& artist,
                            ProgressCallback onProgress,
                            CompletionCallback onComplete);

    // Aborts download for a specific video ID if active
    void cancelDownload(const juce::String& videoId);

    // Listener triggered when a track is successfully saved to the persistent downloads folder
    std::function<void(const juce::File& file, const juce::String& videoId, const juce::String& title, const juce::String& artist)> onTrackSavedToDownloads;

private:
    YouTubeAudioDownloader();
    ~YouTubeAudioDownloader() = default;

    mutable std::mutex mutex;
    std::unordered_set<std::string> activeDownloads;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> cancelFlags;

    JUCE_DECLARE_NON_COPYABLE(YouTubeAudioDownloader)
};
