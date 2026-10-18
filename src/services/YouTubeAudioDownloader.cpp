#include "YouTubeAudioDownloader.h"
#include "YouTubeService.h"
#include "LanguageManager.h"
#include <juce_cryptography/juce_cryptography.h>

namespace
{
constexpr const char* kYtDlpLatestReleaseApi =
    "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest";
constexpr juce::int64 kMaxYtDlpDownloadBytes = 100LL * 1024LL * 1024LL;

std::mutex ytDlpInstallMutex;

juce::String getYtDlpAssetName()
{
#if JUCE_WINDOWS
    return "yt-dlp.exe";
#elif JUCE_MAC
    return "yt-dlp_macos";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "yt-dlp_linux_aarch64";
#else
    return "yt-dlp_linux";
#endif
}

juce::File getManagedYtDlpFile()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("DiscNativePro/bin");
    return appData.getChildFile(JUCE_WINDOWS ? "yt-dlp.exe" : "yt-dlp");
}

bool isValidYouTubeVideoId(const juce::String& id)
{
    return id.length() == 11 && id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
}

juce::String readStreamBounded(juce::InputStream& stream, juce::int64 maxBytes)
{
    juce::MemoryOutputStream output;
    char buffer[8192];
    while (!stream.isExhausted())
    {
        const int count = stream.read(buffer, (int)sizeof(buffer));
        if (count <= 0)
            break;
        if (output.getDataSize() + (size_t)count > (size_t)maxBytes)
            return {};
        output.write(buffer, (size_t)count);
    }
    return output.toString();
}

bool installVerifiedYtDlp()
{
    const std::lock_guard<std::mutex> installLock(ytDlpInstallMutex);
    auto targetFile = getManagedYtDlpFile();
    if (targetFile.existsAsFile() && targetFile.getSize() > 1000000)
        return true;

    auto targetDir = targetFile.getParentDirectory();
    if (!targetDir.createDirectory().wasOk())
        return false;

    juce::URL apiUrl(kYtDlpLatestReleaseApi);
    std::unique_ptr<juce::InputStream> apiStream(apiUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000)
            .withNumRedirectsToFollow(0)
            .withExtraHeaders("User-Agent: DiscNativePro-yt-dlp\n"
                              "Accept: application/vnd.github+json\r\n")));
    if (apiStream == nullptr)
        return false;

    auto releaseJson = juce::JSON::parse(readStreamBounded(*apiStream, 2 * 1024 * 1024));
    if (!releaseJson.isObject())
        return false;

    const auto expectedAsset = getYtDlpAssetName();
    juce::String downloadUrl;
    juce::String expectedDigest;
    if (auto* assets = releaseJson["assets"].getArray())
    {
        for (const auto& asset : *assets)
        {
            if (asset["name"].toString() == expectedAsset)
            {
                downloadUrl = asset["browser_download_url"].toString().trim();
                expectedDigest = asset["digest"].toString().trim().toLowerCase();
                break;
            }
        }
    }

    if (!downloadUrl.startsWithIgnoreCase("https://github.com/yt-dlp/yt-dlp/releases/download/")
        || !downloadUrl.endsWithIgnoreCase("/" + expectedAsset)
        || !expectedDigest.startsWith("sha256:"))
        return false;

    expectedDigest = expectedDigest.substring(7);
    if (expectedDigest.length() != 64 || !expectedDigest.containsOnly("0123456789abcdef"))
        return false;

    juce::URL binaryUrl(downloadUrl);
    std::unique_ptr<juce::InputStream> binaryStream(binaryUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(20000)
            .withNumRedirectsToFollow(5)
            .withExtraHeaders("User-Agent: DiscNativePro-yt-dlp\r\n"
                              "Accept: application/octet-stream\r\n")));
    if (binaryStream == nullptr)
        return false;

    const auto totalLength = binaryStream->getTotalLength();
    if (totalLength > kMaxYtDlpDownloadBytes)
        return false;

    auto tempFile = targetDir.getNonexistentChildFile("yt-dlp", ".download", false);
    std::unique_ptr<juce::FileOutputStream> output(tempFile.createOutputStream());
    if (output == nullptr || !output->openedOk())
        return false;

    juce::HeapBlock<char> buffer(64 * 1024);
    juce::int64 bytesWritten = 0;
    bool failed = false;
    while (!binaryStream->isExhausted())
    {
        const int count = binaryStream->read(buffer.get(), 64 * 1024);
        if (count <= 0)
            break;
        if (bytesWritten + count > kMaxYtDlpDownloadBytes
            || !output->write(buffer.get(), (size_t)count))
        {
            failed = true;
            break;
        }
        bytesWritten += count;
    }
    output->flush();
    output.reset();

    bool valid = !failed && bytesWritten > 1000000 && tempFile.existsAsFile()
              && tempFile.getSize() == bytesWritten
              && (totalLength <= 0 || totalLength == bytesWritten)
              && juce::SHA256(tempFile).toHexString().equalsIgnoreCase(expectedDigest);
#if !JUCE_WINDOWS
    if (valid)
        valid = tempFile.setExecutePermission(true);
#endif

    if (valid)
    {
        targetFile.deleteFile();
        valid = tempFile.moveFileTo(targetFile);
    }

    if (!valid)
        tempFile.deleteFile();
    return valid;
}
} // namespace

YouTubeAudioDownloader& YouTubeAudioDownloader::getInstance()
{
    static YouTubeAudioDownloader instance;
    return instance;
}

YouTubeAudioDownloader::YouTubeAudioDownloader()
{
    getCacheFolder().createDirectory();
}

juce::File YouTubeAudioDownloader::getCacheFolder() const
{
    auto userAppDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto cacheDir = userAppDir.getChildFile("Caches/DiscNativePro/YouTubeAudio");
    return cacheDir;
}

juce::File YouTubeAudioDownloader::getCachedAudioFile(const juce::String& videoId) const
{
    if (videoId.isEmpty())
        return {};

    // 1. Verificar primero si reside en la carpeta permanente de Descargas
    auto dlFolder = YouTubeService::getDownloadsFolder();
    if (dlFolder.isDirectory())
    {
        juce::Array<juce::File> dlFiles;
        dlFolder.findChildFiles(dlFiles, juce::File::findFiles, false, "*" + videoId + "*.m4a");
        for (const auto& f : dlFiles)
        {
            if (f.existsAsFile() && f.getSize() > 10000 && f.getFileName().contains(videoId))
                return f;
        }
    }

    // 2. Verificar en la caché temporal
    auto file = getCacheFolder().getChildFile(videoId + ".m4a");
    if (file.existsAsFile() && file.getSize() > 10000)
        return file;

    return {};
}

bool YouTubeAudioDownloader::isDownloading(const juce::String& videoId) const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return activeDownloads.find(videoId.toStdString()) != activeDownloads.end();
}

juce::String YouTubeAudioDownloader::getBinaryPath() const
{
    const auto managedBinary = getManagedYtDlpFile();
    if (managedBinary.existsAsFile() && managedBinary.getSize() > 1000000)
        return managedBinary.getFullPathName();

#if JUCE_MAC
    // DiscNativePro.app/Contents/Resources/yt-dlp (bundled)
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto bundleRes = exeFile.getParentDirectory().getParentDirectory().getChildFile("Resources/yt-dlp");
    if (bundleRes.existsAsFile())
        return bundleRes.getFullPathName();

    // Homebrew Apple Silicon and Intel
    juce::File brewArm("/opt/homebrew/bin/yt-dlp");
    if (brewArm.existsAsFile())
        return brewArm.getFullPathName();
    juce::File brewIntel("/usr/local/bin/yt-dlp");
    if (brewIntel.existsAsFile())
        return brewIntel.getFullPathName();
#elif JUCE_LINUX
    for (const auto& path : { "/usr/bin/yt-dlp", "/usr/local/bin/yt-dlp" })
    {
        juce::File candidate(path);
        if (candidate.existsAsFile())
            return candidate.getFullPathName();
    }
#endif

    // Search PATH as directories instead of invoking a shell.
    const auto pathValue = juce::SystemStats::getEnvironmentVariable("PATH", {});
    juce::StringArray pathDirectories;
    pathDirectories.addTokens(pathValue, JUCE_WINDOWS ? ";" : ":", "\"");
    const auto binaryName = getYtDlpAssetName();
    for (const auto& directory : pathDirectories)
    {
        auto candidate = juce::File(directory.trim()).getChildFile(binaryName);
        if (candidate.existsAsFile())
            return candidate.getFullPathName();
    }

    return {};
}

void YouTubeAudioDownloader::ensureBinaryInstalledAsync(std::function<void(bool success)> onDone)
{
    juce::Thread::launch([this, onDone]() {
        auto existing = getBinaryPath();
        if (existing.isNotEmpty())
        {
            if (onDone)
                juce::MessageManager::callAsync([onDone]() { onDone(true); });
            return;
        }

        const bool ok = installVerifiedYtDlp();

        if (onDone)
            juce::MessageManager::callAsync([onDone, ok]() { onDone(ok); });
    });
}

void YouTubeAudioDownloader::cancelDownload(const juce::String& videoId)
{
    const std::lock_guard<std::mutex> lock(mutex);
    auto it = cancelFlags.find(videoId.toStdString());
    if (it != cancelFlags.end() && it->second)
    {
        it->second->store(true);
    }
}

void YouTubeAudioDownloader::downloadAudioAsync(const juce::String& videoId,
                                                ProgressCallback onProgress,
                                                CompletionCallback onComplete)
{
    downloadAudioAsync(videoId, {}, {}, onProgress, onComplete);
}

void YouTubeAudioDownloader::downloadAudioAsync(const juce::String& videoId,
                                                const juce::String& title,
                                                const juce::String& artist,
                                                ProgressCallback onProgress,
                                                CompletionCallback onComplete)
{
    if (!isValidYouTubeVideoId(videoId))
    {
        if (onComplete)
            juce::MessageManager::callAsync([onComplete]() {
                onComplete({}, false, LanguageManager::trUtf8("ID de video no válido", "Invalid video ID"));
            });
        return;
    }

    // 1. Instant cache hit
    auto cached = getCachedAudioFile(videoId);
    if (cached.existsAsFile())
    {
        if (onProgress)
            juce::MessageManager::callAsync([onProgress]() {
                onProgress(1.0f, LanguageManager::trUtf8("Audio cargado de caché", "Audio loaded from cache"));
            });

        if (onComplete)
            juce::MessageManager::callAsync([onComplete, cached]() {
                onComplete(cached, true, {});
            });
        return;
    }

    // 2. Launch background download
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (activeDownloads.find(videoId.toStdString()) != activeDownloads.end())
        {
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete({}, false, LanguageManager::trUtf8("La descarga de este video ya está en curso", "This video is already downloading"));
                });
            return;
        }
        activeDownloads.insert(videoId.toStdString());
        cancelFlags[videoId.toStdString()] = cancelFlag;
    }

    juce::Thread::launch([this, videoId, title, artist, cancelFlag, onProgress, onComplete]() {
        // Ensure binary path is resolved
        auto binPath = getBinaryPath();
        if (binPath.isEmpty())
        {
            if (onProgress)
            {
                juce::MessageManager::callAsync([onProgress]() {
                    onProgress(0.05f, LanguageManager::trUtf8("Preparando motor de descarga...", "Preparing download engine..."));
                });
            }

            installVerifiedYtDlp();
            binPath = getBinaryPath();
        }

        if (binPath.isEmpty())
        {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                activeDownloads.erase(videoId.toStdString());
                cancelFlags.erase(videoId.toStdString());
            }
            if (onComplete)
            {
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete({}, false, LanguageManager::trUtf8("No se pudo iniciar el extractor yt-dlp", "Could not start yt-dlp extractor"));
                });
            }
            return;
        }

        auto targetFolder = getCacheFolder();
        targetFolder.createDirectory();
        auto outFile = targetFolder.getChildFile(videoId + ".m4a");
        auto partFile = targetFolder.getChildFile(videoId + ".m4a.part");
        partFile.deleteFile();

        // Download command requesting best native AAC audio stream (m4a container)
        juce::StringArray args;
        args.add(binPath);
        args.add("-f");
        args.add("ba[ext=m4a]/ba");
        args.add("--no-playlist");
        args.add("--newline");
        args.add("-o");
        args.add(outFile.getFullPathName());
        args.add("https://www.youtube.com/watch?v=" + videoId);

        juce::ChildProcess proc;
        if (!proc.start(args))
        {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                activeDownloads.erase(videoId.toStdString());
                cancelFlags.erase(videoId.toStdString());
            }
            if (onComplete)
            {
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete({}, false, LanguageManager::trUtf8("Fallo al iniciar descarga", "Failed to start download process"));
                });
            }
            return;
        }

        // Stream output reader to report progress
        char buffer[1024];
        juce::String currentLine;
        float lastReportedProgress = 0.0f;

        while (proc.isRunning())
        {
            if (cancelFlag->load())
            {
                proc.kill();
                partFile.deleteFile();
                outFile.deleteFile();
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    activeDownloads.erase(videoId.toStdString());
                    cancelFlags.erase(videoId.toStdString());
                }
                return;
            }

            int bytesRead = proc.readProcessOutput(buffer, sizeof(buffer) - 1);
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                currentLine += buffer;

                int newlineIdx;
                while ((newlineIdx = currentLine.indexOfAnyOf("\r\n")) >= 0)
                {
                    auto line = currentLine.substring(0, newlineIdx).trim();
                    currentLine = currentLine.substring(newlineIdx + 1);

                    // Parse yt-dlp download progress: "[download]  45.2% of ..."
                    if (line.contains("[download]") && line.contains("%"))
                    {
                        int pctIdx = line.indexOf("%");
                        if (pctIdx > 0)
                        {
                            int startIdx = pctIdx - 1;
                            while (startIdx >= 0 && (juce::CharacterFunctions::isDigit(line[startIdx]) || line[startIdx] == '.'))
                                --startIdx;

                            auto numStr = line.substring(startIdx + 1, pctIdx).trim();
                            float pct = (float)numStr.getDoubleValue() / 100.0f;
                            pct = juce::jlimit(0.0f, 1.0f, pct);

                            if (pct >= lastReportedProgress + 0.02f || pct >= 0.99f)
                            {
                                lastReportedProgress = pct;
                                if (onProgress)
                                {
                                    juce::MessageManager::callAsync([onProgress, pct]() {
                                        juce::String status = LanguageManager::trUtf8("Descargando audio de YouTube... ", "Downloading YouTube audio... ")
                                                            + juce::String((int)(pct * 100.0f)) + "%";
                                        onProgress(pct, status);
                                    });
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                juce::Thread::sleep(25);
            }
        }

        proc.waitForProcessToFinish(5000);
        juce::uint32 exitCode = proc.getExitCode();

        bool success = (exitCode == 0 || outFile.existsAsFile()) && outFile.getSize() > 10000;

        juce::File finalResultFile = outFile;
        bool savedToDownloads = false;

        if (success && YouTubeService::isSaveDownloadsEnabled())
        {
            auto dlFolder = YouTubeService::getDownloadsFolder();
            if (!dlFolder.exists())
                dlFolder.createDirectory();

            juce::String safeTitle = YouTubeService::sanitizeFilename(title.isNotEmpty() ? title : ("YouTube_" + videoId));
            juce::String safeArtist = YouTubeService::sanitizeFilename(artist);

            juce::String finalFileName;
            if (safeArtist.isNotEmpty() && !safeArtist.equalsIgnoreCase("Artista Local") && !safeArtist.equalsIgnoreCase("Local Artist"))
                finalFileName = safeArtist + " - " + safeTitle + " [" + videoId + "].m4a";
            else
                finalFileName = safeTitle + " [" + videoId + "].m4a";

            auto persistentFile = dlFolder.getChildFile(finalFileName);
            if (!persistentFile.existsAsFile() || persistentFile.getSize() != outFile.getSize())
            {
                outFile.copyFileTo(persistentFile);
            }

            if (persistentFile.existsAsFile() && persistentFile.getSize() > 10000)
            {
                finalResultFile = persistentFile;
                savedToDownloads = true;
            }
        }

        {
            const std::lock_guard<std::mutex> lock(mutex);
            activeDownloads.erase(videoId.toStdString());
            cancelFlags.erase(videoId.toStdString());
        }

        if (onComplete)
        {
            juce::MessageManager::callAsync([this, onComplete, finalResultFile, success, savedToDownloads, videoId, title, artist]() {
                if (success)
                {
                    if (savedToDownloads && onTrackSavedToDownloads)
                    {
                        onTrackSavedToDownloads(finalResultFile, videoId, title, artist);
                    }
                    onComplete(finalResultFile, true, {});
                }
                else
                {
                    onComplete({}, false, LanguageManager::trUtf8("No se pudo obtener el audio de YouTube", "Could not retrieve YouTube audio"));
                }
            });
        }
    });
}
