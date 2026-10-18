#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DeckPlayer.h"
#include "DjMixer.h"
#include "AudioRecorder.h"
#include "SamplerBank.h"

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::ChangeListener,
                    public juce::AsyncUpdater
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool initialise();
    void shutdown();

    // AudioIODeviceCallback interface
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    // ChangeListener & AsyncUpdater interfaces
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void handleAsyncUpdate() override;

    // Audio device resilience & auto-recovery
    bool ensureAudioDeviceRunning();

    // Deck & Mixer access
    DeckPlayer& getDeck(int deckIndex) { return deckIndex == 0 ? deck1 : deck2; }
    DjMixer& getMixer()                { return djMixer; }
    SamplerBank& getSampler()          { return samplerBank; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }
    AudioRecorder& getRecorder()       { return audioRecorder; }

    // Recording convenience methods
    bool toggleRecording(juce::String& outFilePath);
    bool isRecording() const { return audioRecorder.isRecording(); }
    double getRecordingDuration() const { return audioRecorder.getRecordingDurationSeconds(); }
    juce::File getRecordingDirectory() const;
    void setRecordingDirectory(const juce::File& dir);
    juce::File getDefaultRecordingDirectory() const;
    static juce::File getSettingsFile();

    // Tempo Blend & Crossfader
    void setTempoBlend(bool active);
    bool isTempoBlendActive() const { return tempoBlendActive.load(); }
    void setCrossfader(float val);
    void applyTempoBlend(float crossfaderVal);
    void alignBeatPhase(DeckPlayer& masterDeck, DeckPlayer& slaveDeck);

    double getSampleRate() const { return currentSampleRate.load(); }
    int getBlockSize() const     { return currentBlockSize.load(); }

    // Audio Output Channel Routing (0 = Ch 1-2, 1 = Ch 3-4)
    void setMasterChannelPair(int pair)
    {
        const int selected = pair <= 0 ? 0 : 1;
        int routing = outputRouting.load();
        for (;;)
        {
            int cuePair = (routing >> 1) & 1;
            if (cuePair == selected)
                cuePair = 1 - selected;
            const int updated = selected | (cuePair << 1);
            if (outputRouting.compare_exchange_weak(routing, updated))
                break;
        }
    }
    int getMasterChannelPair() const { return outputRouting.load() & 1; }
    void setCueChannelPair(int pair)
    {
        const int selected = pair <= 0 ? 0 : 1;
        int routing = outputRouting.load();
        for (;;)
        {
            int masterPair = routing & 1;
            if (masterPair == selected)
                masterPair = 1 - selected;
            const int updated = masterPair | (selected << 1);
            if (outputRouting.compare_exchange_weak(routing, updated))
                break;
        }
    }
    int getCueChannelPair() const { return (outputRouting.load() >> 1) & 1; }
    int getNumActiveOutputChannels() const { return currentNumOutputChannels.load(); }
    bool is4ChannelOutputAvailable() const { return currentNumOutputChannels.load() >= 4; }

    // Pitch / Tempo Range
    void setPitchRange(float range) {
        deck1.setPitchRange(range);
        deck2.setPitchRange(range);
    }
    float getPitchRange() const { return deck1.getPitchRange(); }

private:
    juce::String initialiseDefaultOutputDevice();

    juce::AudioDeviceManager deviceManager;
    juce::TimeSliceThread diskReaderThread { "DJ Disk Streaming Thread" };
    juce::AudioFormatManager formatManager;

    DeckPlayer deck1;
    DeckPlayer deck2;
    DjMixer djMixer;
    SamplerBank samplerBank;
    AudioRecorder audioRecorder;

    // Preallocated buffers for real-time safety (no malloc in audio callback)
    juce::AudioBuffer<float> deck1Buffer;
    juce::AudioBuffer<float> deck2Buffer;
    juce::AudioBuffer<float> masterBuffer;
    juce::AudioBuffer<float> cueBuffer;

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentBlockSize { 512 };
    std::atomic<int> currentNumOutputChannels { 0 };
    // One atomic keeps the master and cue pairs distinct and consistent.
    // Low bit = master pair; bit 1 = cue pair. Initial route: master 0, cue 1.
    std::atomic<int> outputRouting { 2 };

    std::atomic<bool> tempoBlendActive { false };
    std::atomic<float> currentCrossfaderVal { 0.0f };
    enum class CrossfaderZone { Deck1, Center, Deck2 };
    CrossfaderZone lastCrossfaderZone { CrossfaderZone::Center };
    mutable juce::File customRecordDir;

    std::atomic<bool> isShuttingDown { false };
    std::atomic<bool> isRecoveringDevice { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
