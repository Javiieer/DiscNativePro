#include "AudioRecorder.h"

AudioRecorder::AudioRecorder()
    : juce::Thread("DiscPro Audio Recorder Thread")
{
    fifoBuffer.setSize(2, fifoSize);
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
}

bool AudioRecorder::startRecording(const juce::File& file, double sampleRate, int numChannels)
{
    stopRecording();

    if (isThreadRunning() || writer != nullptr || sampleRate <= 0.0
        || numChannels <= 0 || numChannels > fifoBuffer.getNumChannels())
        return false;

    currentRecordFile = file;
    currentSampleRate = sampleRate;
    channels = numChannels;
    totalRecordedSamples.store(0);
    droppedSamples.store(0);
    durationSeconds.store(0.0);

    if (currentRecordFile.exists())
        return false;

    auto outStream = currentRecordFile.createOutputStream();
    if (outStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;
    writer.reset(wavFormat.createWriterFor(outStream.release(),
                                           sampleRate,
                                           (unsigned int)channels,
                                           24, // 24-bit high quality
                                           {},
                                           0));

    if (writer == nullptr)
    {
        currentRecordFile.deleteFile();
        return false;
    }

    fifo.reset();
    fifoBuffer.clear();
    if (!startThread(juce::Thread::Priority::normal))
    {
        writer.reset();
        currentRecordFile.deleteFile();
        return false;
    }
    recording.store(true, std::memory_order_release);
    return true;
}

void AudioRecorder::stopRecording()
{
    const bool wasRecording = recording.exchange(false, std::memory_order_acq_rel);
    if (wasRecording)
        wakeEvent.signal();

    while (activePushes.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);

    if (isThreadRunning() && !stopThread(3000))
    {
        DBG("AudioRecorder: worker did not stop before timeout; writer kept alive.");
        return;
    }

    if (writer != nullptr)
    {
        flushRemainingBuffer();
        writer.reset();
    }
}

void AudioRecorder::pushAudioBlock(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    activePushes.fetch_add(1, std::memory_order_acquire);
    struct PushGuard
    {
        std::atomic<int>& counter;
        ~PushGuard() { counter.fetch_sub(1, std::memory_order_release); }
    } guard { activePushes };

    if (!recording.load(std::memory_order_acquire) || numSamples <= 0)
        return;

    numSamples = juce::jmin(numSamples, buffer.getNumSamples());
    if (numSamples <= 0)
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    int numChannelsToCopy = juce::jmin(channels, buffer.getNumChannels());

    if (size1 > 0)
    {
        for (int ch = 0; ch < numChannelsToCopy; ++ch)
            fifoBuffer.copyFrom(ch, start1, buffer, ch, 0, size1);
    }

    if (size2 > 0)
    {
        for (int ch = 0; ch < numChannelsToCopy; ++ch)
            fifoBuffer.copyFrom(ch, start2, buffer, ch, size1, size2);
    }

    fifo.finishedWrite(size1 + size2);

    const int acceptedSamples = size1 + size2;
    const int dropped = numSamples - acceptedSamples;
    if (dropped > 0)
        droppedSamples.fetch_add(dropped, std::memory_order_relaxed);
    const int64_t total = totalRecordedSamples.fetch_add(acceptedSamples, std::memory_order_relaxed) + acceptedSamples;
    if (currentSampleRate > 0.0)
        durationSeconds.store((double)total / currentSampleRate, std::memory_order_relaxed);

    wakeEvent.signal();
}

void AudioRecorder::run()
{
    juce::AudioBuffer<float> tempBuffer(channels, 2048);

    while (!threadShouldExit())
    {
        wakeEvent.wait(20);

        int numReady = fifo.getNumReady();
        while (numReady > 0 && !threadShouldExit())
        {
            int blockToRead = juce::jmin(numReady, tempBuffer.getNumSamples());
            int start1, size1, start2, size2;
            fifo.prepareToRead(blockToRead, start1, size1, start2, size2);

            if (size1 > 0)
            {
                for (int ch = 0; ch < channels; ++ch)
                    tempBuffer.copyFrom(ch, 0, fifoBuffer, ch, start1, size1);
            }

            if (size2 > 0)
            {
                for (int ch = 0; ch < channels; ++ch)
                    tempBuffer.copyFrom(ch, size1, fifoBuffer, ch, start2, size2);
            }

            fifo.finishedRead(size1 + size2);

            if (writer != nullptr && (size1 + size2) > 0)
            {
                writer->writeFromAudioSampleBuffer(tempBuffer, 0, size1 + size2);
            }

            numReady = fifo.getNumReady();
        }
    }
}

void AudioRecorder::flushRemainingBuffer()
{
    int numReady = fifo.getNumReady();
    if (numReady <= 0 || writer == nullptr)
        return;

    juce::AudioBuffer<float> temp(channels, numReady);
    int start1, size1, start2, size2;
    fifo.prepareToRead(numReady, start1, size1, start2, size2);

    if (size1 > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
            temp.copyFrom(ch, 0, fifoBuffer, ch, start1, size1);
    }
    if (size2 > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
            temp.copyFrom(ch, size1, fifoBuffer, ch, start2, size2);
    }

    fifo.finishedRead(size1 + size2);
    writer->writeFromAudioSampleBuffer(temp, 0, size1 + size2);
}
