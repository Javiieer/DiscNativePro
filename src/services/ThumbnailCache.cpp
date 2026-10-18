#include "ThumbnailCache.h"

namespace
{
bool isAllowedArtworkUrl(const juce::String& value)
{
    const juce::URL url(value);
    if (!url.isWellFormed() || !url.toString(false).startsWithIgnoreCase("https://"))
        return false;

    const auto domain = url.getDomain().toLowerCase();
    return domain == "ytimg.com" || domain.endsWith(".ytimg.com")
        || domain == "sndcdn.com" || domain.endsWith(".sndcdn.com");
}
}

ThumbnailCache& ThumbnailCache::getInstance()
{
    static ThumbnailCache instance;
    return instance;
}

ThumbnailCache::ThumbnailCache()
{
    getCacheFolder().createDirectory();
}

juce::File ThumbnailCache::getCacheFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DiscNativePro/Thumbnails");
}

void ThumbnailCache::clearMemoryCache()
{
    std::scoped_lock lock(cacheMutex);
    memoryCache.clear();
}

juce::Image ThumbnailCache::getThumbnail(const juce::String& videoId,
                                        const juce::String& url,
                                        std::function<void(const juce::String&)> onReady)
{
    auto safeKey = videoId.isNotEmpty() ? videoId : url;
    if (safeKey.isEmpty())
        return {};

    std::string key = safeKey.toStdString();

    {
        std::scoped_lock lock(cacheMutex);
        auto it = memoryCache.find(key);
        if (it != memoryCache.end())
            return it->second;

        if (pendingRequests.find(key) != pendingRequests.end())
            return {};
    }

    // Check disk cache
    auto cacheFile = getCacheFolder().getChildFile(juce::File::createLegalFileName(safeKey) + ".jpg");
    if (cacheFile.existsAsFile() && cacheFile.getSize() > 200)
    {
        auto img = juce::ImageFileFormat::loadFrom(cacheFile);
        if (img.isValid())
        {
            auto scaled = img.rescaled(160, 90, juce::Graphics::mediumResamplingQuality);
            auto finalImg = scaled.isValid() ? scaled : img;

            std::scoped_lock lock(cacheMutex);
            memoryCache[key] = finalImg;
            return finalImg;
        }
    }

    // Need to download
    juce::String downloadUrl = url;
    if (videoId.isNotEmpty() && (downloadUrl.isEmpty() || downloadUrl.contains(".webp") || !downloadUrl.contains(".jpg")))
    {
        downloadUrl = "https://i.ytimg.com/vi/" + videoId + "/mqdefault.jpg";
    }

    if (downloadUrl.isNotEmpty())
    {
        if (!isAllowedArtworkUrl(downloadUrl))
            return {};
        {
            std::scoped_lock lock(cacheMutex);
            pendingRequests.insert(key);
        }
        downloadAsync(videoId, downloadUrl, std::move(onReady));
    }

    return {};
}

void ThumbnailCache::downloadAsync(const juce::String& videoId,
                                   const juce::String& url,
                                   std::function<void(const juce::String&)> onReadyCallback)
{
    auto safeKey = videoId.isNotEmpty() ? videoId : url;
    auto cacheFile = getCacheFolder().getChildFile(juce::File::createLegalFileName(safeKey) + ".jpg");
    auto tmpFile = getCacheFolder().getChildFile(juce::File::createLegalFileName(safeKey) + ".tmp");

    juce::Thread::launch([this, videoId, url, safeKey, cacheFile, tmpFile, cb = std::move(onReadyCallback)]() mutable {
        bool downloadOk = false;
        if (isAllowedArtworkUrl(url))
        {
            juce::URL juceUrl(url);
            std::unique_ptr<juce::InputStream> stream(juceUrl.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(6000)
                    .withNumRedirectsToFollow(0)));

            if (stream != nullptr)
            {
                tmpFile.deleteFile();
                juce::FileOutputStream fos(tmpFile);
                if (fos.openedOk())
                {
                    constexpr juce::int64 maxImageBytes = 10 * 1024 * 1024;
                    char buffer[8192];
                    juce::int64 bytesWritten = 0;
                    while (!stream->isExhausted() && bytesWritten <= maxImageBytes)
                    {
                        const int bytesRead = stream->read(buffer, (int)sizeof(buffer));
                        if (bytesRead <= 0)
                            break;
                        if (bytesWritten + bytesRead > maxImageBytes)
                        {
                            bytesWritten = maxImageBytes + 1;
                            break;
                        }
                        fos.write(buffer, (size_t)bytesRead);
                        bytesWritten += bytesRead;
                    }
                    fos.flush();
                    downloadOk = bytesWritten > 200 && bytesWritten <= maxImageBytes;
                }
            }
        }

        juce::Image finalImg;
        if (downloadOk && tmpFile.existsAsFile())
        {
            auto rawImg = juce::ImageFileFormat::loadFrom(tmpFile);

            if (rawImg.isValid())
            {
                auto scaled = rawImg.rescaled(160, 90, juce::Graphics::mediumResamplingQuality);
                finalImg = scaled.isValid() ? scaled : rawImg;
                cacheFile.deleteFile();
                tmpFile.moveFileTo(cacheFile);
            }
        }
        tmpFile.deleteFile();

        juce::MessageManager::callAsync([this, safeKey, videoId, finalImg, readyCb = std::move(cb)]() {
            std::string key = safeKey.toStdString();
            {
                std::scoped_lock lock(cacheMutex);
                if (finalImg.isValid())
                    memoryCache[key] = finalImg;
                pendingRequests.erase(key);
            }

            if (readyCb)
                readyCb(videoId);
        });
    });
}
