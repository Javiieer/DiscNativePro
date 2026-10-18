#include "AudioEngine.h"
#if JUCE_WINDOWS
#include "MediaFoundationAudioFormat_win.h"
#endif

AudioEngine::AudioEngine()
    : deck1(formatManager, diskReaderThread),
      deck2(formatManager, diskReaderThread)
{
    // Register audio formats (WAV, AIFF, FLAC, OGG, and CoreAudio MP3/AAC/M4A on macOS are all in basic formats)
    formatManager.registerBasicFormats();
#if JUCE_WINDOWS
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false);
#endif
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialise()
{
    isShuttingDown.store(false);

    // Start background disk streaming thread (priority normal for smooth file prefetching)
    if (!diskReaderThread.startThread(juce::Thread::Priority::normal))
    {
        DBG("Could not start the disk reader thread");
        return false;
    }

    // Start with a safe stereo pair. Some drivers reject a four-channel request
    // even though their normal stereo output is available.
    auto err = initialiseDefaultOutputDevice();
    if (!err.isEmpty())
    {
        DBG("Audio device error: " << err);
        diskReaderThread.stopThread(-1);
        return false;
    }

    deviceManager.addAudioCallback(this);
    deviceManager.addChangeListener(this);
    return true;
}

juce::String AudioEngine::initialiseDefaultOutputDevice()
{
    currentNumOutputChannels.store(0);

    auto error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
        return error;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No default audio output device is available.";

    auto availableOutputs = device->getOutputChannelNames().size();
    if (availableOutputs >= 4)
    {
        auto stereoSetup = deviceManager.getAudioDeviceSetup();
        auto fourChannelSetup = stereoSetup;
        fourChannelSetup.outputChannels.clear();
        fourChannelSetup.outputChannels.setRange(0, 4, true);

        auto fourChannelError = deviceManager.setAudioDeviceSetup(fourChannelSetup, true);
        if (fourChannelError.isNotEmpty())
        {
            DBG("[AudioEngine] Four-channel output was rejected; keeping stereo: " << fourChannelError);
            auto stereoError = deviceManager.setAudioDeviceSetup(stereoSetup, true);
            if (stereoError.isNotEmpty())
                return "Could not restore stereo output after four-channel setup failed: " + stereoError;
        }
    }

    device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "The audio device closed while its output channels were being configured.";

    const int activeChannels = device->getActiveOutputChannels().countNumberOfSetBits();
    currentNumOutputChannels.store(activeChannels);
    if (activeChannels < 2)
        return "The selected audio device has fewer than two active output channels.";

    DBG("[AudioEngine] Active output channels: " << activeChannels << " on " << device->getName());
    return {};
}

void AudioEngine::shutdown()
{
    isShuttingDown.store(true);
    cancelPendingUpdate();
    audioRecorder.stopRecording();
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    // Deck sources depend on this thread. Wait for a clean exit rather than
    // allowing JUCE to force-kill it while a source could still be using them.
    diskReaderThread.stopThread(-1);
}

juce::File AudioEngine::getSettingsFile()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("DiscNativePro");
    if (!appData.exists())
        appData.createDirectory();
    return appData.getChildFile("settings.json");
}

juce::File AudioEngine::getDefaultRecordingDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("DiscPro_Recordings");
}

juce::File AudioEngine::getRecordingDirectory() const
{
    if (customRecordDir.isDirectory())
        return customRecordDir;

    auto file = getSettingsFile();
    if (file.existsAsFile())
    {
        auto json = juce::JSON::parse(file);
        if (json.isObject() && json.hasProperty("recordingDirectory"))
        {
            auto path = json["recordingDirectory"].toString().trim();
            if (path.isNotEmpty())
            {
                juce::File dir(path);
                if (dir.isDirectory() || dir.exists())
                {
                    customRecordDir = dir;
                    return customRecordDir;
                }
            }
        }
    }
    return getDefaultRecordingDirectory();
}

void AudioEngine::setRecordingDirectory(const juce::File& dir)
{
    customRecordDir = dir;
    auto file = getSettingsFile();
    juce::var json;
    if (file.existsAsFile())
        json = juce::JSON::parse(file);
    if (!json.isObject())
        json = juce::var(new juce::DynamicObject());

    if (auto* obj = json.getDynamicObject())
    {
        if (dir == getDefaultRecordingDirectory() || !dir.exists())
            obj->removeProperty("recordingDirectory");
        else
            obj->setProperty("recordingDirectory", dir.getFullPathName());
    }
    file.replaceWithText(juce::JSON::toString(json));
}

bool AudioEngine::toggleRecording(juce::String& outFilePath)
{
    if (audioRecorder.isRecording())
    {
        outFilePath = audioRecorder.getCurrentFile().getFullPathName();
        audioRecorder.stopRecording();
        return false; // Stopped
    }

    auto recDir = getRecordingDirectory();
    if (!recDir.exists())
        recDir.createDirectory();

    auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S");
    auto recFile = recDir.getChildFile("DiscPro_Mix_" + timestamp + ".wav");

    bool success = audioRecorder.startRecording(recFile, currentSampleRate.load(), 2);
    if (success)
    {
        outFilePath = recFile.getFullPathName();
        return true; // Started
    }

    return false;
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const double sampleRate = device->getCurrentSampleRate();
    const int blockSize = device->getCurrentBufferSizeSamples();
    currentSampleRate.store(sampleRate);
    currentBlockSize.store(blockSize);
    currentNumOutputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());

    // Preallocate buffers for zero runtime heap allocation (real-time safe)
    deck1Buffer.setSize(2, blockSize, false, true, true);
    deck2Buffer.setSize(2, blockSize, false, true, true);
    masterBuffer.setSize(2, blockSize, false, true, true);
    cueBuffer.setSize(2, blockSize, false, true, true);

    deck1.prepareToPlay(blockSize, sampleRate);
    deck2.prepareToPlay(blockSize, sampleRate);
    djMixer.prepare(sampleRate, blockSize);
}

void AudioEngine::audioDeviceStopped()
{
    currentNumOutputChannels.store(0);
    audioRecorder.stopRecording();
    deck1.releaseResources();
    deck2.releaseResources();
    djMixer.reset();

    // Si el dispositivo se detuvo inesperadamente (ej. WebKit o CoreAudio),
    // programar recuperacion asincrona en el hilo de mensajes
    if (!isShuttingDown.load())
    {
        triggerAsyncUpdate();
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager && !isShuttingDown.load())
    {
        ensureAudioDeviceRunning();
    }
}

void AudioEngine::handleAsyncUpdate()
{
    if (!isShuttingDown.load())
    {
        ensureAudioDeviceRunning();
    }
}

bool AudioEngine::ensureAudioDeviceRunning()
{
    if (isShuttingDown.load())
        return false;

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev != nullptr && dev->isPlaying())
        return true;

    bool expected = false;
    if (!isRecoveringDevice.compare_exchange_strong(expected, true))
        return false;

    DBG("[AudioEngine] Audio device is not running (dev=" << (dev != nullptr ? dev->getName() : "null")
        << ", playing=" << (dev != nullptr && dev->isPlaying()) << "). Recovering...");

    // 1. Si hay un dispositivo existente detenido, cerrarlo explicitamente primero
    // para que restartLastAudioDevice no sea un no-op en JUCE.
    deviceManager.closeAudioDevice();
    deviceManager.restartLastAudioDevice();

    dev = deviceManager.getCurrentAudioDevice();
    if (dev != nullptr && dev->isPlaying())
    {
        DBG("[AudioEngine] Restored audio device successfully via restartLastAudioDevice");
        isRecoveringDevice.store(false);
        return true;
    }

    // 2. Si restartLastAudioDevice falló, re-inicializar en estéreo y ampliar
    // solamente si el dispositivo acepta cuatro salidas.
    auto err = initialiseDefaultOutputDevice();
    if (err.isEmpty())
    {
        deviceManager.addAudioCallback(this);
        DBG("[AudioEngine] Restored audio device successfully via initialiseWithDefaultDevices");
        isRecoveringDevice.store(false);
        return true;
    }

    DBG("[AudioEngine] Error restoring audio device: " << err);
    isRecoveringDevice.store(false);
    return false;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* /*inputChannelData*/,
                                                  int /*numInputChannels*/,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (outputChannelData == nullptr || numOutputChannels == 0 || numSamples <= 0)
        return;

    if (numSamples > masterBuffer.getNumSamples()
        || numSamples > deck1Buffer.getNumSamples()
        || numSamples > deck2Buffer.getNumSamples()
        || numSamples > cueBuffer.getNumSamples())
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                std::memset(outputChannelData[ch], 0, sizeof(float) * (size_t)numSamples);
        return;
    }

    // Ensure our internal buffers match current block samples
    deck1Buffer.clear(0, numSamples);
    deck2Buffer.clear(0, numSamples);
    cueBuffer.clear(0, numSamples);

    // 1. Fetch audio from streaming deck sources
    juce::AudioSourceChannelInfo d1Info(&deck1Buffer, 0, numSamples);
    deck1.getNextAudioBlock(d1Info);

    juce::AudioSourceChannelInfo d2Info(&deck2Buffer, 0, numSamples);
    deck2.getNextAudioBlock(d2Info);

    // 2. Process DSP: Channel EQ, Color Filters, Deck FX Chains, Channel Faders, Crossfader, Master Gain, Cue Preescucha & Meters
    djMixer.setDeckBpm(0, static_cast<float>(deck1.getBpm()));
    djMixer.setDeckBpm(1, static_cast<float>(deck2.getBpm()));
    djMixer.process(deck1Buffer, deck2Buffer, masterBuffer, cueBuffer, numSamples);

    // 2b. Sampler one-shots al master post-crossfader (entran a la grabación)
    samplerBank.process(masterBuffer, numSamples);

    // 3. Push to master recorder (lock-free, zero allocation)
    audioRecorder.pushAudioBlock(masterBuffer, numSamples);

    // 4. Write to physical audio output (DAC / CoreAudio)
    const int routing = outputRouting.load();
    const int masterPair = routing & 1;
    const int cuePair = (routing >> 1) & 1;

    if (numOutputChannels >= 4)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            auto* out = outputChannelData[ch];
            if (out == nullptr)
                continue;
            if (ch == masterPair * 2)
            {
                std::memcpy(out, masterBuffer.getReadPointer(0), sizeof(float) * (size_t)numSamples);
            }
            else if (ch == masterPair * 2 + 1)
            {
                std::memcpy(out, masterBuffer.getReadPointer(1), sizeof(float) * (size_t)numSamples);
            }
            else if (ch == cuePair * 2)
            {
                std::memcpy(out, cueBuffer.getReadPointer(0), sizeof(float) * (size_t)numSamples);
            }
            else if (ch == cuePair * 2 + 1)
            {
                std::memcpy(out, cueBuffer.getReadPointer(1), sizeof(float) * (size_t)numSamples);
            }
            else
            {
                std::memset(out, 0, sizeof(float) * (size_t)numSamples);
            }
        }
    }
    else
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (ch < 2)
            {
                auto* out = outputChannelData[ch];
                if (out == nullptr)
                    continue;
                const auto* src = masterBuffer.getReadPointer(ch);
                std::memcpy(out, src, sizeof(float) * (size_t)numSamples);
            }
            else
            {
                if (outputChannelData[ch] != nullptr)
                    std::memset(outputChannelData[ch], 0, sizeof(float) * (size_t)numSamples);
            }
        }
    }
}

void AudioEngine::setTempoBlend(bool active)
{
    tempoBlendActive.store(active);
    if (active)
    {
        applyTempoBlend(currentCrossfaderVal.load());
    }
}

void AudioEngine::setCrossfader(float val)
{
    val = juce::jlimit(-1.0f, 1.0f, val);
    currentCrossfaderVal.store(val);
    djMixer.setCrossfader(val);

    if (tempoBlendActive.load() && deck1.isLoaded() && deck2.isLoaded())
    {
        CrossfaderZone currentZone = (val <= -0.75f) ? CrossfaderZone::Deck1
                                   : (val >=  0.75f) ? CrossfaderZone::Deck2
                                                     : CrossfaderZone::Center;

        // Check for zone transition into center to align beat phase cleanly
        if (lastCrossfaderZone == CrossfaderZone::Deck1 && currentZone == CrossfaderZone::Center
            && deck1.isPlaying() && deck2.isPlaying())
        {
            alignBeatPhase(deck1, deck2);
        }
        else if (lastCrossfaderZone == CrossfaderZone::Deck2 && currentZone == CrossfaderZone::Center
                 && deck1.isPlaying() && deck2.isPlaying())
        {
            alignBeatPhase(deck2, deck1);
        }
        lastCrossfaderZone = currentZone;

        applyTempoBlend(val);
    }
}

void AudioEngine::applyTempoBlend(float val)
{
    if (!tempoBlendActive.load()) return;
    if (!deck1.isLoaded() || !deck2.isLoaded()) return;

    double b1 = deck1.getOriginalBpm();
    double b2 = deck2.getOriginalBpm();
    if (b1 <= 0.0) b1 = 124.0;
    if (b2 <= 0.0) b2 = 126.0;

    // Normalized crossfader: 0.0 (Deck 1) to 1.0 (Deck 2)
    double t = juce::jlimit(0.0, 1.0, (val + 1.0) / 2.0);

    // Interpolated target BPM for the entire mix
    double targetBpm = b1 * (1.0 - t) + b2 * t;

    // Calculate required pitch percent offsets
    double pitchPercent1 = ((targetBpm / b1) - 1.0) * 100.0;
    double pitchPercent2 = ((targetBpm / b2) - 1.0) * 100.0;

    deck1.setPitchPercent((float)pitchPercent1);
    deck2.setPitchPercent((float)pitchPercent2);
}

void AudioEngine::alignBeatPhase(DeckPlayer& masterDeck, DeckPlayer& slaveDeck)
{
    if (!masterDeck.isLoaded() || !slaveDeck.isLoaded()) return;
    if (masterDeck.isScratching() || slaveDeck.isScratching()) return; // no pelear con la mano

    double bpm = masterDeck.getBpm();
    if (bpm <= 0.0) bpm = 120.0;
    double beatInterval = 60.0 / bpm;

    double masterTime = masterDeck.getPosition();
    double masterPhase = std::fmod(masterTime, beatInterval);
    if (masterPhase < 0.0) masterPhase += beatInterval;

    double slaveTime = slaveDeck.getPosition();
    double slavePhase = std::fmod(slaveTime, beatInterval);
    if (slavePhase < 0.0) slavePhase += beatInterval;

    double correction = masterPhase - slavePhase;
    if (correction > beatInterval * 0.5) correction -= beatInterval;
    if (correction < -beatInterval * 0.5) correction += beatInterval;

    slaveDeck.seekRelative(correction);
}
