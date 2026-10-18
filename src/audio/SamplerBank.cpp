#include "SamplerBank.h"
#include <cmath>
#include <cstring>

SamplerBank::~SamplerBank()
{
    for (auto& slot : slots)
        slot.playing.store(false, std::memory_order_release);

    for (auto& slot : slots)
    {
        auto* oldSample = slot.sample.exchange(nullptr, std::memory_order_acq_rel);
        while (activeReaders.load(std::memory_order_acquire) != 0)
            juce::Thread::sleep(1);
        delete oldSample;
    }
}

bool SamplerBank::loadSlot(int index, const juce::File& file,
                           juce::AudioFormatManager& formatManager, double deviceSampleRate)
{
    if (index < 0 || index >= numSlots || deviceSampleRate <= 0.0)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    juce::int64 srcLen = reader->lengthInSamples;
    juce::int64 maxLen = (juce::int64)(reader->sampleRate * maxSampleSeconds);
    if (srcLen > maxLen)
        srcLen = maxLen;

    const int srcCh = (int)reader->numChannels;
    const double lengthRatio = deviceSampleRate / reader->sampleRate;
    const double resamplingRatio = reader->sampleRate / deviceSampleRate;
    const int outLen = juce::jmax(1, (int)((double)srcLen * lengthRatio)) + 64;

    juce::AudioBuffer<float> buf(2, outLen);
    buf.clear();

    if (std::abs(lengthRatio - 1.0) < 1e-6)
    {
        const int n = (int)juce::jmin<juce::int64>(srcLen, outLen);
        reader->read(&buf, 0, n, 0, true, true);
        if (srcCh < 2) // mono: duplica determinista
            std::memcpy(buf.getWritePointer(1), buf.getReadPointer(0), sizeof(float) * (size_t)n);
    }
    else
    {
        juce::AudioFormatReaderSource src(reader.release(), true);
        juce::ResamplingAudioSource rs(&src, false, 2);
        rs.prepareToPlay(512, deviceSampleRate);
        rs.setResamplingRatio(resamplingRatio);
        juce::AudioSourceChannelInfo info(&buf, 0, outLen);
        rs.getNextAudioBlock(info);
        if (srcCh < 2)
            std::memcpy(buf.getWritePointer(1), buf.getReadPointer(0), sizeof(float) * (size_t)outLen);
    }

    // Fade-out de 8 ms horneado (sin clics al terminar, costo cero en RT)
    const int fade = juce::jmin(outLen, (int)(deviceSampleRate * 0.008));
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < fade; ++i)
            d[outLen - 1 - i] *= (float)i / (float)fade;
    }

    auto& slot = slots[(size_t)index];
    auto* replacement = new juce::AudioBuffer<float>(std::move(buf));
    slot.playing.store(false, std::memory_order_release);
    slot.readPos.store(0, std::memory_order_relaxed);
    slot.name = file.getFileNameWithoutExtension();
    slot.sampleLength.store(replacement->getNumSamples(), std::memory_order_release);
    auto* oldSample = slot.sample.exchange(replacement, std::memory_order_acq_rel);
    while (activeReaders.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);
    delete oldSample;
    lastDeviceSr.store(deviceSampleRate, std::memory_order_release);
    return true;
}

void SamplerBank::freeSlot(int index)
{
    auto& slot = slots[(size_t)index];
    slot.playing.store(false, std::memory_order_release);
    slot.readPos.store(0, std::memory_order_relaxed);
    slot.sampleLength.store(0, std::memory_order_release);
    auto* oldSample = slot.sample.exchange(nullptr, std::memory_order_acq_rel);
    while (activeReaders.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);
    delete oldSample;
    slot.name.clear();
}

void SamplerBank::unloadSlot(int index)
{
    if (index < 0 || index >= numSlots)
        return;
    freeSlot(index);
}

void SamplerBank::trigger(int index)
{
    if (index < 0 || index >= numSlots)
        return;
    auto& slot = slots[(size_t)index];
    if (slot.sampleLength.load(std::memory_order_acquire) <= 0)
        return;
    slot.readPos.store(0);
    slot.playing.store(true);
}

void SamplerBank::stopSlot(int index)
{
    if (index < 0 || index >= numSlots)
        return;
    slots[(size_t)index].playing.store(false);
}

void SamplerBank::stopAll()
{
    for (auto& slot : slots)
        slot.playing.store(false);
}

bool SamplerBank::isLoaded(int index) const
{
    if (index < 0 || index >= numSlots)
        return false;
    return slots[(size_t)index].sampleLength.load(std::memory_order_acquire) > 0;
}

juce::String SamplerBank::getName(int index) const
{
    if (index < 0 || index >= numSlots)
        return {};
    return slots[(size_t)index].name;
}

double SamplerBank::getDurationSeconds(int index) const
{
    const double deviceSampleRate = lastDeviceSr.load(std::memory_order_acquire);
    if (index < 0 || index >= numSlots || deviceSampleRate <= 0.0)
        return 0.0;
    return (double)slots[(size_t)index].sampleLength.load(std::memory_order_acquire) / deviceSampleRate;
}

void SamplerBank::process(juce::AudioBuffer<float>& master, int numSamples)
{
    if (numSamples <= 0)
        return;

    activeReaders.fetch_add(1, std::memory_order_acquire);
    struct ReaderGuard
    {
        std::atomic<int>& counter;
        ~ReaderGuard() { counter.fetch_sub(1, std::memory_order_release); }
    } guard { activeReaders };

    const float g = masterVolume.load();
    if (g <= 0.0f)
    {
        // Volumen 0: igual avanza (los one-shots siguen su curso en silencio)
        for (auto& slot : slots)
        {
            if (!slot.playing.load())
                continue;
            auto* sample = slot.sample.load(std::memory_order_acquire);
            if (sample == nullptr)
            {
                slot.playing.store(false, std::memory_order_relaxed);
                continue;
            }
            const int len = sample->getNumSamples();
            const int next = slot.readPos.load() + numSamples;
            slot.readPos.store(next);
            if (next >= len)
                slot.playing.store(false);
        }
        return;
    }

    const int mCh = master.getNumChannels();
    for (auto& slot : slots)
    {
        if (!slot.playing.load())
            continue;
        auto* sample = slot.sample.load(std::memory_order_acquire);
        if (sample == nullptr)
        {
            slot.playing.store(false, std::memory_order_relaxed);
            continue;
        }
        const int len = sample->getNumSamples();
        const int pos = slot.readPos.load();
        const int avail = len - pos;
        if (avail <= 0)
        {
            slot.playing.store(false);
            continue;
        }
        const int n = juce::jmin(numSamples, avail);
        const int sCh = sample->getNumChannels();
        for (int ch = 0; ch < mCh; ++ch)
            master.addFrom(ch, 0, *sample, ch % juce::jmax(1, sCh), pos, n, g);
        const int next = pos + n;
        slot.readPos.store(next);
        if (next >= len)
            slot.playing.store(false);
    }
}
