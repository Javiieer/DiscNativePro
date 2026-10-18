#include "SoundCloudService.h"
#include <juce_events/juce_events.h>
#include <thread>
#include <mutex>
#include <map>
#include <vector>

namespace
{
    std::mutex scImageCacheMutex;
    std::mutex scClientIdMutex;
    std::map<juce::String, juce::Image> scArtworkCache;
    juce::String customClientId;
    const char* defaultClientId = "kR7dAfT5UXQgmgde1pLjfsRGiaoANLvT";

    bool isAllowedSoundCloudUrl(const juce::String& value)
    {
        const juce::URL url(value);
        if (!url.isWellFormed() || !url.toString(false).startsWithIgnoreCase("https://"))
            return false;

        const auto host = url.getDomain().toLowerCase();
        return host == "soundcloud.com" || host.endsWith(".soundcloud.com")
            || host == "sndcdn.com" || host.endsWith(".sndcdn.com");
    }

    bool isValidTrackId(const juce::String& id)
    {
        return id.isNotEmpty() && id.length() <= 20 && id.containsOnly("0123456789");
    }

    juce::String readBounded(juce::InputStream& stream, juce::int64 maxBytes)
    {
        juce::MemoryOutputStream output;
        char buffer[8192];
        while (!stream.isExhausted())
        {
            const int bytesRead = stream.read(buffer, (int)sizeof(buffer));
            if (bytesRead <= 0)
                break;
            if (output.getDataSize() + (size_t)bytesRead > (size_t)maxBytes)
                return {};
            output.write(buffer, (size_t)bytesRead);
        }
        return output.toString();
    }

    bool appendBounded(juce::FileOutputStream& output, juce::InputStream& input,
                       juce::int64& totalBytes, juce::int64 maxBytes)
    {
        char buffer[32 * 1024];
        while (!input.isExhausted())
        {
            const int bytesRead = input.read(buffer, (int)sizeof(buffer));
            if (bytesRead <= 0)
                break;
            if (totalBytes + bytesRead > maxBytes || !output.write(buffer, (size_t)bytesRead))
                return false;
            totalBytes += bytesRead;
        }
        return true;
    }

    juce::Image readImageBounded(juce::InputStream& input, juce::int64 maxBytes)
    {
        juce::MemoryOutputStream output;
        char buffer[8192];
        while (!input.isExhausted())
        {
            const int bytesRead = input.read(buffer, (int)sizeof(buffer));
            if (bytesRead <= 0)
                break;
            if (output.getDataSize() + (size_t)bytesRead > (size_t)maxBytes)
                return {};
            output.write(buffer, (size_t)bytesRead);
        }
        return juce::ImageFileFormat::loadFrom(output.getData(), output.getDataSize());
    }
}

juce::String SoundCloudService::getClientId()
{
    const std::lock_guard<std::mutex> lock(scClientIdMutex);
    return customClientId.isNotEmpty() ? customClientId : juce::String(defaultClientId);
}

void SoundCloudService::setClientId(const juce::String& id)
{
    const std::lock_guard<std::mutex> lock(scClientIdMutex);
    customClientId = id.trim();
}

juce::File SoundCloudService::getCacheDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("DiscNativePro")
                   .getChildFile("soundcloud_cache");
    if (!dir.exists())
        dir.createDirectory();
    return dir;
}

std::vector<SoundCloudTrackItem> SoundCloudService::search(const juce::String& query)
{
    std::vector<SoundCloudTrackItem> results;
    auto q = query.trim();
    if (q.isEmpty())
        return results;

    juce::String encoded = juce::URL::addEscapeChars(q, true);
    juce::String urlStr = "https://api-v2.soundcloud.com/search/tracks?q=" + encoded
                        + "&client_id=" + juce::URL::addEscapeChars(getClientId(), true)
                        + "&limit=40";

    juce::URL searchUrl(urlStr);
    auto stream = searchUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                  .withConnectionTimeoutMs(8000)
                                                  .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
    if (stream == nullptr)
        return results;

    juce::String jsonText = readBounded(*stream, 4 * 1024 * 1024);
    auto parsed = juce::JSON::parse(jsonText);
    if (!parsed.isObject() || !parsed.hasProperty("collection"))
        return results;

    auto collection = parsed["collection"];
    if (!collection.isArray())
        return results;

    for (int i = 0; i < collection.size(); ++i)
    {
        auto item = collection[i];
        if (!item.isObject())
            continue;

        SoundCloudTrackItem track;
        track.id = item["id"].toString();
        if (!isValidTrackId(track.id))
            continue;
        track.title = item["title"].toString();

        if (item.hasProperty("user") && item["user"].isObject())
            track.user = item["user"]["username"].toString();

        int durMs = item["duration"].toString().getIntValue();
        track.durationSeconds = durMs / 1000;
        int mins = track.durationSeconds / 60;
        int secs = track.durationSeconds % 60;
        track.durationText = juce::String::formatted("%02d:%02d", mins, secs);

        track.artworkUrl = item["artwork_url"].toString();
        if (track.artworkUrl.isEmpty() && item.hasProperty("user") && item["user"].isObject())
            track.artworkUrl = item["user"]["avatar_url"].toString();

        track.permalinkUrl = item["permalink_url"].toString();

        // Extract stream auth URLs: prefer progressive, fallback to HLS AAC (fMP4)
        if (item.hasProperty("media") && item["media"].isObject() && item["media"].hasProperty("transcodings"))
        {
            auto transcodings = item["media"]["transcodings"];
            if (transcodings.isArray())
            {
                for (int t = 0; t < transcodings.size(); ++t)
                {
                    auto fmt = transcodings[t];
                    if (fmt.isObject() && fmt.hasProperty("format") && fmt["format"].isObject())
                    {
                        auto proto = fmt["format"]["protocol"].toString();
                        auto mime = fmt["format"]["mime_type"].toString();

                        if (proto.equalsIgnoreCase("progressive") && track.progressiveStreamAuthUrl.isEmpty())
                        {
                            track.progressiveStreamAuthUrl = fmt["url"].toString();
                        }
                        else if (proto.equalsIgnoreCase("hls") && mime.contains("mp4") && track.hlsStreamAuthUrl.isEmpty())
                        {
                            track.hlsStreamAuthUrl = fmt["url"].toString();
                        }
                    }
                }
                track.isHls = track.progressiveStreamAuthUrl.isEmpty() && track.hlsStreamAuthUrl.isNotEmpty();
            }
        }

        if (track.title.isNotEmpty() && (track.progressiveStreamAuthUrl.isNotEmpty() || track.hlsStreamAuthUrl.isNotEmpty()))
            results.push_back(track);
    }

    return results;
}

void SoundCloudService::searchAsync(const juce::String& query,
                                    std::function<void(const std::vector<SoundCloudTrackItem>&)> onComplete)
{
    std::thread([query, cb = std::move(onComplete)]() {
        auto res = search(query);
        juce::MessageManager::callAsync([cb, res]() {
            if (cb)
                cb(res);
        });
    }).detach();
}

juce::File SoundCloudService::downloadHlsStream(const juce::String& hlsAuthUrl, const juce::String& trackId)
{
    if (!isValidTrackId(trackId) || !isAllowedSoundCloudUrl(hlsAuthUrl))
        return {};

    auto cacheDir = getCacheDirectory();
    auto targetFile = cacheDir.getChildFile("sc_" + trackId + ".m4a");
    if (targetFile.existsAsFile() && targetFile.getSize() > 10000)
        return targetFile;

    juce::String authUrlStr = hlsAuthUrl;
    if (!authUrlStr.contains("client_id="))
        authUrlStr += (authUrlStr.contains("?") ? "&" : "?") + juce::String("client_id=")
                    + juce::URL::addEscapeChars(getClientId(), true);

    juce::URL authUrl(authUrlStr);
    if (!isAllowedSoundCloudUrl(authUrlStr))
        return {};
    auto authStream = authUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                    .withConnectionTimeoutMs(8000)
                                                    .withNumRedirectsToFollow(0)
                                                    .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
    if (authStream == nullptr)
        return {};

    auto parsed = juce::JSON::parse(readBounded(*authStream, 1024 * 1024));
    if (!parsed.isObject() || !parsed.hasProperty("url"))
        return {};

    juce::String m3u8Url = parsed["url"].toString();
    if (!isAllowedSoundCloudUrl(m3u8Url))
        return {};

    juce::URL playlistUrl(m3u8Url);
    auto playlistStream = playlistUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                            .withConnectionTimeoutMs(10000)
                                                            .withNumRedirectsToFollow(0)
                                                            .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
    if (playlistStream == nullptr)
        return {};

    juce::String m3u8Text = readBounded(*playlistStream, 2 * 1024 * 1024);
    if (m3u8Text.isEmpty())
        return {};

    // 1. Extract init.mp4 url: #EXT-X-MAP:URI="...url..."
    juce::String initUrl;
    int mapIdx = m3u8Text.indexOf("#EXT-X-MAP:URI=\"");
    if (mapIdx >= 0)
    {
        int start = mapIdx + 16;
        int end = m3u8Text.indexOf(start, "\"");
        if (end > start)
            initUrl = m3u8Text.substring(start, end);
    }

    // 2. Extract segments (lines not starting with #)
    juce::StringArray lines;
    lines.addLines(m3u8Text);
    juce::StringArray segmentUrls;
    for (auto& l : lines)
    {
        auto trimmed = l.trim();
        if (trimmed.isNotEmpty() && !trimmed.startsWithChar('#'))
        {
            if (!isAllowedSoundCloudUrl(trimmed))
                return {};
            segmentUrls.add(trimmed);
        }
    }

    if (initUrl.isNotEmpty() && !isAllowedSoundCloudUrl(initUrl))
        return {};

    if (segmentUrls.isEmpty())
        return {};

    auto tempFile = targetFile.getSiblingFile(targetFile.getFileName() + ".tmp");
    tempFile.deleteFile();
    juce::int64 totalBytes = 0;
    constexpr juce::int64 maxTrackBytes = 256LL * 1024LL * 1024LL;
    bool transferOk = true;

    {
        juce::FileOutputStream fos(tempFile);
        if (!fos.openedOk())
            return {};

        // Download init.mp4 first
        if (initUrl.isNotEmpty())
        {
            juce::URL u(initUrl);
            auto s = u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs(8000)
                                             .withNumRedirectsToFollow(0)
                                             .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
            transferOk = s != nullptr && appendBounded(fos, *s, totalBytes, maxTrackBytes);
        }

        // Download segments
        for (auto& segUrl : segmentUrls)
        {
            if (!transferOk)
                break;
            juce::URL u(segUrl);
            auto s = u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs(10000)
                                             .withNumRedirectsToFollow(0)
                                             .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
            transferOk = s != nullptr && appendBounded(fos, *s, totalBytes, maxTrackBytes);
        }
        fos.flush();
    }

    if (!transferOk)
    {
        tempFile.deleteFile();
        return {};
    }

    if (tempFile.existsAsFile() && tempFile.getSize() > 10000 && tempFile.moveFileTo(targetFile))
    {
        return targetFile;
    }

    tempFile.deleteFile();
    return {};
}

void SoundCloudService::downloadTrackAsync(const SoundCloudTrackItem& item,
                                           std::function<void(const juce::File&)> onDownloaded)
{
    if (!isValidTrackId(item.id)
        || (item.progressiveStreamAuthUrl.isEmpty() && item.hlsStreamAuthUrl.isEmpty()))
    {
        if (onDownloaded)
            juce::MessageManager::callAsync([onDownloaded]() { onDownloaded(juce::File()); });
        return;
    }

    auto cacheDir = getCacheDirectory();
    const bool hasProgressiveStream = item.progressiveStreamAuthUrl.isNotEmpty();
    const juce::String ext = hasProgressiveStream ? ".mp3" : ".m4a";
    auto targetFile = cacheDir.getChildFile("sc_" + item.id + ext);

    auto altTarget = cacheDir.getChildFile("sc_" + item.id + (hasProgressiveStream ? ".m4a" : ".mp3"));
    if (altTarget.existsAsFile() && altTarget.getSize() > 10000)
        targetFile = altTarget;

    if (targetFile.existsAsFile() && targetFile.getSize() > 10000)
    {
        juce::MessageManager::callAsync([onDownloaded, targetFile]() {
            if (onDownloaded)
                onDownloaded(targetFile);
        });
        return;
    }

    std::thread([item, targetFile, cb = std::move(onDownloaded)]() {
        bool downloadOk = false;
        auto tempFile = targetFile.getSiblingFile(targetFile.getFileName() + ".tmp");
        tempFile.deleteFile();

        // 1. Try Progressive download if available
        if (item.progressiveStreamAuthUrl.isNotEmpty())
        {
            juce::String authUrlStr = item.progressiveStreamAuthUrl;
            if (!isAllowedSoundCloudUrl(authUrlStr))
                authUrlStr = {};
            if (authUrlStr.isNotEmpty() && !authUrlStr.contains("client_id="))
                authUrlStr += (authUrlStr.contains("?") ? "&" : "?") + juce::String("client_id=")
                            + juce::URL::addEscapeChars(getClientId(), true);

            juce::URL authUrl(authUrlStr);
            auto authStream = authUrlStr.isNotEmpty() ? authUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                            .withConnectionTimeoutMs(8000)
                                                            .withNumRedirectsToFollow(0)
                                                            .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n")) : nullptr;

            juce::String directStreamUrl;
            if (authStream != nullptr)
            {
                auto parsed = juce::JSON::parse(readBounded(*authStream, 1024 * 1024));
                if (parsed.isObject() && parsed.hasProperty("url"))
                    directStreamUrl = parsed["url"].toString();
            }

            if (isAllowedSoundCloudUrl(directStreamUrl))
            {
                juce::URL downloadUrl(directStreamUrl);
                auto dlStream = downloadUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                                  .withConnectionTimeoutMs(15000)
                                                                  .withNumRedirectsToFollow(0)
                                                                  .withExtraHeaders("User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"));
                if (dlStream != nullptr)
                {
                    {
                        juce::FileOutputStream fos(tempFile);
                        if (fos.openedOk())
                        {
                            juce::int64 totalBytes = 0;
                            constexpr juce::int64 maxTrackBytes = 256LL * 1024LL * 1024LL;
                            downloadOk = appendBounded(fos, *dlStream, totalBytes, maxTrackBytes);
                            fos.flush();
                            downloadOk = downloadOk && totalBytes > 10000;
                        }
                    }

                    if (downloadOk && tempFile.existsAsFile() && tempFile.getSize() > 10000
                        && tempFile.moveFileTo(targetFile))
                    {
                        downloadOk = true;
                    }
                    else
                    {
                        downloadOk = false;
                    }
                }
            }
        }

        // 2. Fallback to HLS download if progressive was not available or failed
        if (!downloadOk && item.hlsStreamAuthUrl.isNotEmpty())
        {
            auto hlsFile = downloadHlsStream(item.hlsStreamAuthUrl, item.id);
            if (hlsFile.existsAsFile() && hlsFile.getSize() > 10000)
            {
                downloadOk = true;
                juce::MessageManager::callAsync([cb, hlsFile]() {
                    if (cb)
                        cb(hlsFile);
                });
                return;
            }
        }

        if (downloadOk)
        {
            juce::MessageManager::callAsync([cb, targetFile]() {
                if (cb)
                    cb(targetFile);
            });
        }
        else
        {
            tempFile.deleteFile();
            juce::MessageManager::callAsync([cb]() {
                if (cb)
                    cb(juce::File());
            });
        }
    }).detach();
}

void SoundCloudService::loadArtworkAsync(const juce::String& artworkUrl,
                                         std::function<void(const juce::Image&)> onImageLoaded)
{
    if (!isAllowedSoundCloudUrl(artworkUrl))
        return;

    {
        std::lock_guard<std::mutex> lock(scImageCacheMutex);
        auto it = scArtworkCache.find(artworkUrl);
        if (it != scArtworkCache.end() && it->second.isValid())
        {
            juce::MessageManager::callAsync([onImageLoaded, img = it->second]() {
                if (onImageLoaded)
                    onImageLoaded(img);
            });
            return;
        }
    }

    std::thread([artworkUrl, cb = std::move(onImageLoaded)]() {
        juce::URL url(artworkUrl);
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                .withConnectionTimeoutMs(6000)
                                                .withNumRedirectsToFollow(0));
        juce::Image img;
        if (stream != nullptr)
            img = readImageBounded(*stream, 10 * 1024 * 1024);

        if (img.isValid())
        {
            std::lock_guard<std::mutex> lock(scImageCacheMutex);
            scArtworkCache[artworkUrl] = img;
        }

        juce::MessageManager::callAsync([cb, img]() {
            if (cb)
                cb(img);
        });
    }).detach();
}
