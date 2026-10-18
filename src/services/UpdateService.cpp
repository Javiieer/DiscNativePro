#include "UpdateService.h"
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

namespace
{
constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/axel-molina/DiscNativePro/releases/latest";
constexpr const char* kReleasesApi =
    "https://api.github.com/repos/axel-molina/DiscNativePro/releases?per_page=15";

#if JUCE_MAC
constexpr const char* kAssetName = "DiscNativePro-macOS-Universal.zip";
#elif JUCE_WINDOWS
constexpr const char* kAssetName = "DiscNativePro-Windows-x64-Setup.exe";
#else
constexpr const char* kAssetName = "DiscNativePro-Linux-x64.tar.gz";
#endif

ReleaseItem parseReleaseItem(const juce::var& releaseObj)
{
    ReleaseItem item;
    if (!releaseObj.isObject())
        return item;

    item.tagName = releaseObj["tag_name"].toString().trim();
    item.version = item.tagName;
    if (item.version.startsWithChar('v') || item.version.startsWithChar('V'))
        item.version = item.version.substring(1);
    item.name = releaseObj["name"].toString().trim();
    if (item.name.isEmpty())
        item.name = item.tagName;
    item.releaseNotes = releaseObj["body"].toString();
    item.pageUrl = releaseObj["html_url"].toString();
    item.downloadUrl = item.pageUrl;

    if (auto* assets = releaseObj["assets"].getArray())
    {
        for (auto& a : *assets)
        {
            auto name = a["name"].toString();
            auto dl = a["browser_download_url"].toString();
            auto digest = a["digest"].toString().trim().toLowerCase();
            auto hash = digest.startsWith("sha256:") ? digest.substring(7) : juce::String();
            if (name == kAssetName && dl.isNotEmpty()
                && hash.length() == 64 && hash.containsOnly("0123456789abcdef"))
            {
                item.downloadUrl = dl;
                item.assetName = name;
                item.assetSha256 = digest;
                break;
            }
        }

    }

    return item;
}
} // namespace

juce::String UpdateService::getCurrentVersion()
{
#ifdef DISCNATIVEPRO_VERSION
    return DISCNATIVEPRO_VERSION;
#else
    return "1.0.0-dev";
#endif
}

int UpdateService::compareVersions(const juce::String& a, const juce::String& b)
{
    // Compara solo componentes numéricos ("1.2.0" vs "1.10.0" => -1).
    // Los sufijos (-dev, -rc1) se ignoran aquí; se resuelven en isNewerVersion.
    auto numParts = [](const juce::String& v) {
        juce::StringArray parts;
        parts.addTokens(v.upToFirstOccurrenceOf("-", false, false), ".", "");
        return parts;
    };

    auto pa = numParts(a.trim());
    auto pb = numParts(b.trim());
    int n = juce::jmax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i)
    {
        int va = i < pa.size() ? pa[i].getIntValue() : 0;
        int vb = i < pb.size() ? pb[i].getIntValue() : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

bool UpdateService::isNewerVersion(const juce::String& latest, const juce::String& current)
{
    juce::String l = latest.trim();
    juce::String c = current.trim();
    if (l.startsWithChar('v') || l.startsWithChar('V'))
        l = l.substring(1);
    if (c.startsWithChar('v') || c.startsWithChar('V'))
        c = c.substring(1);

    int cmp = compareVersions(l, c);
    // Comparación solo numérica: un empate ("1.0.0-3-gabc123" vs "1.0.0")
    // NO es update. Solo una versión numéricamente mayor avisa.
    // (Los builds locales ya no reportan un "-dev" genérico: CMake estampa
    // git describe, así que no hacen falta reglas especiales de sufijos.)
    return cmp > 0;
}

juce::File UpdateService::getSettingsFile()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("DiscNativePro");
    if (!appData.exists())
        appData.createDirectory();
    return appData.getChildFile("settings.json");
}

juce::String UpdateService::getSkippedTag()
{
    auto file = getSettingsFile();
    if (!file.existsAsFile())
        return {};
    auto json = juce::JSON::parse(file);
    if (json.isObject() && json.hasProperty("skippedUpdateTag"))
        return json["skippedUpdateTag"].toString();
    return {};
}

void UpdateService::setSkippedTag(const juce::String& tag)
{
    auto file = getSettingsFile();
    juce::var json;
    if (file.existsAsFile())
        json = juce::JSON::parse(file);
    if (!json.isObject())
        json = juce::var(new juce::DynamicObject());

    if (auto* obj = json.getDynamicObject())
    {
        if (tag.isEmpty())
            obj->removeProperty("skippedUpdateTag");
        else
            obj->setProperty("skippedUpdateTag", tag);
    }
    file.replaceWithText(juce::JSON::toString(json));
}

void UpdateService::checkForUpdatesAsync(UpdateCallback onComplete)
{
    juce::Thread::launch([onComplete]() {
        UpdateInfo info;
        info.currentVersion = getCurrentVersion();

        // 1. Intentar listar releases recientes
        juce::URL listUrl(kReleasesApi);
        std::unique_ptr<juce::InputStream> stream(listUrl.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(10000)
                .withExtraHeaders("User-Agent: DiscNativePro-Updater\r\n"
                                  "Accept: application/vnd.github+json\r\n")));

        if (stream != nullptr)
        {
            auto json = juce::JSON::parse(stream->readEntireStreamAsString());
            if (json.isArray())
            {
                auto* arr = json.getArray();
                for (const auto& elem : *arr)
                {
                    auto item = parseReleaseItem(elem);
                    if (item.version.isNotEmpty())
                        info.allReleases.push_back(item);
                }
            }
            else if (json.isObject())
            {
                auto item = parseReleaseItem(json);
                if (item.version.isNotEmpty())
                    info.allReleases.push_back(item);
            }
        }

        // 2. Si falló la lista o vino vacía, intentar /releases/latest directamente
        if (info.allReleases.empty())
        {
            juce::URL latestUrl(kLatestReleaseApi);
            std::unique_ptr<juce::InputStream> latestStream(latestUrl.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(10000)
                    .withExtraHeaders("User-Agent: DiscNativePro-Updater\r\n"
                                      "Accept: application/vnd.github+json\r\n")));

            if (latestStream != nullptr)
            {
                auto json = juce::JSON::parse(latestStream->readEntireStreamAsString());
                if (json.isObject())
                {
                    auto item = parseReleaseItem(json);
                    if (item.version.isNotEmpty())
                        info.allReleases.push_back(item);
                }
            }
        }

        // 3. Procesar release más reciente
        if (!info.allReleases.empty())
        {
            const auto& latest = info.allReleases.front();
            info.latestTag = latest.tagName;
            info.latestVersion = latest.version;
            info.releaseName = latest.name;
            info.releaseNotes = latest.releaseNotes;
            info.pageUrl = latest.pageUrl;
            info.downloadUrl = latest.downloadUrl;
            info.assetName = latest.assetName;
            info.assetSha256 = latest.assetSha256;

            if (info.latestVersion.isNotEmpty()
                && info.latestTag != getSkippedTag()
                && isNewerVersion(info.latestVersion, info.currentVersion))
            {
                info.available = true;
            }
        }

        juce::MessageManager::callAsync([onComplete, info]() { onComplete(info); });
    });
}

void UpdateService::downloadAndOpenReleaseAsset(
    const juce::String& downloadUrl,
    const juce::String& assetName,
    const juce::String& expectedSha256,
    std::function<void(float progress, const juce::String& statusText)> onProgress,
    std::function<void(bool success, const juce::String& errorMessage, const juce::File& file)> onComplete)
{
    juce::Thread::launch([downloadUrl, assetName, expectedSha256, onProgress, onComplete]() {
        if (downloadUrl.isEmpty())
        {
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(false, "URL de descarga vacía.", {});
                });
            return;
        }

        // Si es una página web general en vez de un archivo instalable, abrir navegador
        if (!downloadUrl.endsWithIgnoreCase(".zip") &&
            !downloadUrl.endsWithIgnoreCase(".dmg") &&
            !downloadUrl.endsWithIgnoreCase(".exe") &&
            !downloadUrl.endsWithIgnoreCase(".tar.gz"))
        {
            if (!downloadUrl.startsWithIgnoreCase("https://github.com/axel-molina/DiscNativePro/releases/"))
            {
                if (onComplete)
                    juce::MessageManager::callAsync([onComplete]() {
                        onComplete(false, "El enlace de actualización no coincide con la página de versiones esperada.", {});
                    });
                return;
            }
            juce::URL(downloadUrl).launchInDefaultBrowser();
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(true, {}, {});
                });
            return;
        }

        const auto expectedPrefix = juce::String("https://github.com/axel-molina/DiscNativePro/releases/download/");
        auto expectedHash = expectedSha256.trim().toLowerCase();
        if (expectedHash.startsWith("sha256:"))
            expectedHash = expectedHash.substring(7);
        if (!downloadUrl.startsWithIgnoreCase(expectedPrefix) || assetName != kAssetName
            || expectedHash.length() != 64
            || !expectedHash.containsOnly("0123456789abcdef"))
        {
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(false, "No hay un enlace y SHA-256 válidos publicados para verificar este archivo.", {});
                });
            return;
        }

        auto dlFolder = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads");
        if (!dlFolder.exists())
            dlFolder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

        juce::String safeFileName = juce::File::createLegalFileName(assetName);
        if (safeFileName.isEmpty())
            safeFileName = "DiscNativePro-Update.zip";

        auto targetFile = dlFolder.getChildFile(safeFileName);
        if (targetFile.existsAsFile())
        {
            auto baseName = targetFile.getFileNameWithoutExtension();
            auto extension = targetFile.getFileExtension();
            int suffix = 1;
            do
            {
                targetFile = dlFolder.getChildFile(baseName + "-" + juce::String(suffix++) + extension);
            }
            while (targetFile.exists());
        }

        juce::URL url(downloadUrl);
        std::unique_ptr<juce::InputStream> stream(url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(15000)
                .withNumRedirectsToFollow(10)
                .withExtraHeaders("User-Agent: DiscNativePro-Updater\r\n"
                                  "Accept: application/octet-stream\r\n")));

        if (stream == nullptr)
        {
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(false, "No se pudo conectar al servidor de descargas.", {});
                });
            return;
        }

        juce::int64 totalLength = stream->getTotalLength();
        auto tempFile = juce::File::createTempFile("DiscNativePro-Update");
        std::unique_ptr<juce::FileOutputStream> out(tempFile.createOutputStream());
        if (out == nullptr || !out->openedOk())
        {
            tempFile.deleteFile();
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(false, "No se pudo crear el archivo temporal de actualización.", {});
                });
            return;
        }

        constexpr juce::int64 bufferSize = 64 * 1024;
        constexpr juce::int64 maxDownloadBytes = 1024LL * 1024LL * 1024LL;
        juce::HeapBlock<char> buffer((size_t)bufferSize);
        juce::int64 bytesWritten = 0;

        bool writeFailed = false;
        while (!stream->isExhausted() && bytesWritten <= maxDownloadBytes)
        {
            int read = stream->read(buffer.get(), (int)bufferSize);
            if (read <= 0)
                break;

            if (bytesWritten + read > maxDownloadBytes || !out->write(buffer.get(), (size_t)read))
            {
                writeFailed = true;
                break;
            }
            bytesWritten += read;

            if (onProgress && totalLength > 0)
            {
                float prog = (float)bytesWritten / (float)totalLength;
                juce::String status = juce::String::formatted("%.1f MB / %.1f MB",
                                                              (double)bytesWritten / (1024.0 * 1024.0),
                                                              (double)totalLength / (1024.0 * 1024.0));
                juce::MessageManager::callAsync([onProgress, prog, status]() {
                    onProgress(prog, status);
                });
            }
        }

        out->flush();
        out.reset();

        bool success = !writeFailed && tempFile.existsAsFile() && tempFile.getSize() > 1000
                    && (totalLength <= 0 || bytesWritten == totalLength);
        if (success)
            success = juce::SHA256(tempFile).toHexString().equalsIgnoreCase(expectedHash);
        if (success)
        {
            success = tempFile.moveFileTo(targetFile);
        }
        if (success)
        {
            juce::MessageManager::callAsync([onComplete, targetFile]() {
                targetFile.revealToUser();

                if (onComplete)
                    onComplete(true, {}, targetFile);
            });
        }
        else
        {
            tempFile.deleteFile();
            if (onComplete)
                juce::MessageManager::callAsync([onComplete]() {
                    onComplete(false, "El archivo está incompleto o su SHA-256 no coincide con el publicado por GitHub.", {});
                });
        }
    });
}
