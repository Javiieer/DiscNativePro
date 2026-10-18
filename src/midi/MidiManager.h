#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include "../audio/AudioEngine.h"
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>

class MidiManager : public juce::MidiInputCallback,
                    private juce::Timer
{
public:
    MidiManager(AudioEngine& audioEngine);
    ~MidiManager() override;

    void initialise();
    void shutdown();
    void rescanDevices();

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    bool isAnyDeviceConnected() const { return deviceConnected; }
    juce::StringArray getConnectedDeviceNames() const;
    void sendMidiMessage(const juce::MidiMessage& message);

    void setOnMidiActivity(std::function<void(const juce::String&)> callback) { setCallback(midiActivityCallback, std::move(callback)); }
    void setOnDevicesChanged(std::function<void()> callback) { setCallback(devicesChangedCallback, std::move(callback)); }
    void setOnBrowseRotate(std::function<void(int)> callback) { setCallback(browseRotateCallback, std::move(callback)); }
    void setOnBrowseClick(std::function<void()> callback) { setCallback(browseClickCallback, std::move(callback)); }
    void setOnLoadTrack(std::function<void(int)> callback) { setCallback(loadTrackCallback, std::move(callback)); }
    void setOnPlayPause(std::function<void(int)> callback) { setCallback(playPauseCallback, std::move(callback)); }
    void setOnCue(std::function<void(int)> callback) { setCallback(cueCallback, std::move(callback)); }

private:
    template <typename Callback>
    struct CallbackHandle
    {
        std::shared_ptr<Callback> callback;
        uint64_t generation { 0 };
        explicit operator bool() const { return callback != nullptr; }
    };

    template <typename Callback>
    void setCallback(std::shared_ptr<Callback>& slot, Callback callback)
    {
        const std::lock_guard<std::mutex> lock(callbackMutex);
        slot = callback ? std::make_shared<Callback>(std::move(callback)) : nullptr;
        callbackGenerationState->fetch_add(1, std::memory_order_acq_rel);
    }

    template <typename Callback>
    CallbackHandle<Callback> getCallback(const std::shared_ptr<Callback>& slot) const
    {
        const std::lock_guard<std::mutex> lock(callbackMutex);
        return { slot, callbackGenerationState->load(std::memory_order_acquire) };
    }

    template <typename Callback, typename... Args>
    void dispatchCallback(const CallbackHandle<Callback>& handle, Args... args)
    {
        if (!handle)
            return;
        auto callback = handle.callback;
        auto generationState = callbackGenerationState;
        const auto generation = handle.generation;
        juce::MessageManager::callAsync([callback, generationState, generation, args...]() mutable {
            if (generationState->load(std::memory_order_acquire) != generation)
                return;
            (*callback)(args...);
        });
    }

    AudioEngine& engine;
    bool deviceConnected { false };
    juce::StringArray connectedDevices;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    mutable std::mutex callbackMutex;
    std::shared_ptr<std::atomic<uint64_t>> callbackGenerationState {
        std::make_shared<std::atomic<uint64_t>>(0) };
    std::shared_ptr<std::function<void(const juce::String&)>> midiActivityCallback;
    std::shared_ptr<std::function<void()>> devicesChangedCallback;
    std::shared_ptr<std::function<void(int)>> browseRotateCallback;
    std::shared_ptr<std::function<void()>> browseClickCallback;
    std::shared_ptr<std::function<void(int)>> loadTrackCallback;
    std::shared_ptr<std::function<void(int)>> playPauseCallback;
    std::shared_ptr<std::function<void(int)>> cueCallback;

    // 14-bit CC Tempo Fader tracking (MSB defaults to 64, LSB defaults to 0 -> center: 8192)
    int tempoMsb[4] { 64, 64, 64, 64 };
    int tempoLsb[4] { 0, 0, 0, 0 };
    void apply14BitTempo(int deckIndex);

    // Jog Wheel handling & pitch bend decay
    void handleJogWheel(int deckIndex, int cc, int val);
    void timerCallback() override;
    bool jogBendActive[2] { false, false };
    int jogBendTicks[2] { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiManager)
};
