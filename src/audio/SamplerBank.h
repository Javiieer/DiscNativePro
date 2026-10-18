#pragma once

// Banco de sampler one-shot (8 pads): samples en memoria (device-rate),
// retrigger monofónico por slot, mezcla al master post-crossfader.
// Hilo UI carga/libera; hilo de audio solo lee + atómicos.
// Sin persistencia en v1 (pads vacíos al abrir).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

struct SamplerSlot
{
    std::atomic<juce::AudioBuffer<float>*> sample { nullptr }; // immutable while published
    std::atomic<int> sampleLength { 0 };
    std::atomic<int> readPos { 0 };
    std::atomic<bool> playing { false };
    juce::String name;
};

class SamplerBank
{
public:
    static constexpr int numSlots = 8;
    static constexpr double maxSampleSeconds = 30.0;

    SamplerBank() = default;
    ~SamplerBank();

    // Hilo UI. Devuelve false si el archivo no se pudo decodificar.
    bool loadSlot(int index, const juce::File& file,
                  juce::AudioFormatManager& formatManager, double deviceSampleRate);
    void unloadSlot(int index);

    void trigger(int index); // retrigger (si suena, reinicia)
    void stopSlot(int index);
    void stopAll();

    bool isLoaded(int index) const;
    juce::String getName(int index) const;      // hilo UI
    double getDurationSeconds(int index) const; // hilo UI

    void setMasterVolume(float v) { masterVolume.store(juce::jlimit(0.0f, 2.0f, v)); }
    float getMasterVolume() const { return masterVolume.load(); }

    // Hilo de audio: suma al master. Sin allocs ni locks.
    void process(juce::AudioBuffer<float>& master, int numSamples);

private:
    void freeSlot(int index); // swaps ownership off the audio thread

    SamplerSlot slots[numSlots];
    std::atomic<int> activeReaders { 0 };
    std::atomic<float> masterVolume { 0.8f };
    std::atomic<double> lastDeviceSr { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerBank)
};
