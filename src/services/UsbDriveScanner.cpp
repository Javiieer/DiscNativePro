#include "UsbDriveScanner.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

juce::String UsbDriveScanner::formatBytes(juce::int64 bytes)
{
    if (bytes <= 0)
        return {};

    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double tb = gb * 1024.0;

    auto b = static_cast<double>(bytes);
    if (b >= tb)
        return juce::String(b / tb, 1) + " TB";
    if (b >= gb)
        return juce::String(b / gb, 1) + " GB";
    if (b >= mb)
        return juce::String(b / mb, 0) + " MB";
    return juce::String(b / kb, 0) + " KB";
}

bool UsbDriveScanner::isValidAudioFile(const juce::File& file)
{
    if (file.isDirectory() || file.isHidden())
        return false;

    auto ext = file.getFileExtension().toLowerCase();
    return (ext == ".mp3"  || ext == ".wav"  || ext == ".aif"  ||
            ext == ".aiff" || ext == ".flac" || ext == ".m4a"  ||
            ext == ".aac"  || ext == ".ogg");
}

std::vector<UsbDrive> UsbDriveScanner::getConnectedDrives()
{
    std::vector<UsbDrive> drives;

#if JUCE_MAC
    juce::File volumesDir("/Volumes");
    if (volumesDir.isDirectory())
    {
        juce::Array<juce::File> list;
        volumesDir.findChildFiles(list, juce::File::findDirectories, false);

        for (const auto& vol : list)
        {
            if (vol.isHidden() || vol.isSymbolicLink())
                continue;

            auto name = vol.getFileName();
            // Ignore system boot partitions and APFS internal data volume
            if (name.equalsIgnoreCase("Macintosh HD") || name.equalsIgnoreCase("Data") || name.startsWithIgnoreCase("Recovery"))
                continue;

            UsbDrive d;
            d.id = vol.getFullPathName();
            d.root = vol;
            d.name = name;
            auto freeBytes = vol.getBytesFreeOnVolume();
            if (freeBytes > 0)
                d.sizeDesc = formatBytes(freeBytes) + " libre";
            d.isRemovable = true;
            drives.push_back(d);
        }
    }

#elif JUCE_WINDOWS
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots(roots);

    for (const auto& r : roots)
    {
        auto path = r.getFullPathName();
        auto driveType = GetDriveTypeW(path.toWideCharPointer());

        // Do not classify every secondary fixed disk as a USB volume. On Windows
        // this commonly includes the user's internal D: data drive.
        bool isRemovable = (driveType == DRIVE_REMOVABLE);

        if (isRemovable)
        {
            UsbDrive d;
            d.id = path;
            d.root = r;
            auto label = r.getVolumeLabel().trim();
            d.name = label.isNotEmpty() ? (label + " (" + path.substring(0, 2) + ")") : ("Unidad USB (" + path.substring(0, 2) + ")");
            auto freeBytes = r.getBytesFreeOnVolume();
            if (freeBytes > 0)
                d.sizeDesc = formatBytes(freeBytes) + " libre";
            d.isRemovable = isRemovable;
            drives.push_back(d);
        }
    }

#elif JUCE_LINUX
    // Standard Linux automount points
    juce::StringArray mountPaths = { "/media", "/run/media", "/mnt" };
    for (const auto& basePath : mountPaths)
    {
        juce::File base(basePath);
        if (!base.isDirectory())
            continue;

        juce::Array<juce::File> subdirs;
        base.findChildFiles(subdirs, juce::File::findDirectories, false);

        for (const auto& userOrDrive : subdirs)
        {
            // Might be /media/<username>/<drivename>
            juce::Array<juce::File> userDrives;
            userOrDrive.findChildFiles(userDrives, juce::File::findDirectories, false);

            if (userDrives.isEmpty())
            {
                // userOrDrive is the drive itself
                UsbDrive d;
                d.id = userOrDrive.getFullPathName();
                d.root = userOrDrive;
                d.name = userOrDrive.getFileName();
                auto freeBytes = userOrDrive.getBytesFreeOnVolume();
                if (freeBytes > 0)
                    d.sizeDesc = formatBytes(freeBytes) + " libre";
                d.isRemovable = true;
                drives.push_back(d);
            }
            else
            {
                for (const auto& dFolder : userDrives)
                {
                    UsbDrive d;
                    d.id = dFolder.getFullPathName();
                    d.root = dFolder;
                    d.name = dFolder.getFileName();
                    auto freeBytes = dFolder.getBytesFreeOnVolume();
                    if (freeBytes > 0)
                        d.sizeDesc = formatBytes(freeBytes) + " libre";
                    d.isRemovable = true;
                    drives.push_back(d);
                }
            }
        }
    }
#endif

    return drives;
}
