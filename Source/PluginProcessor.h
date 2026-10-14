#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/TransientSplit.h"
#include "dsp/StreamEQ.h"
#include "dsp/Saturator.h"
#include "Params.h"

class PirateEQ2Processor : public juce::AudioProcessor
{
public:
    PirateEQ2Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "P2EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Test loop (Standalone only): replaces the input with a looping file.
    bool loadTestLoop (const juce::File& f);
    void stopTestLoop() { loopOn = false; }
    bool isTestLoopOn() const { return loopOn.load(); }
    juce::String testLoopName;
    juce::File testLoopFile;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Metering / analyzer (written by audio thread, read by UI)
    std::atomic<float> peak[2] {}, rms[2] {};
    static constexpr int anSize = 4096;
    std::array<float, anSize> anBuf[2] {};
    std::atomic<int> anWrite { 0 };
    std::atomic<double> currentSampleRate { 44100.0 };

private:
    TransientSplit split;                      // stereo, one shared (linked) mask
    std::array<StreamEQ, 2> eq;                 // [0] transient, [1] tonal
    Biquad soloBp[2];                           // band-solo listening filter per stream
    std::array<Saturator, 2> sat;               // [0] transient, [1] tonal (identical latency)
    juce::AudioBuffer<float> trBuf, tnBuf;      // per-stream work buffers (post split/EQ)
    juce::SmoothedValue<float> gainT, gainN;

    juce::AudioBuffer<float> loopBuf;
    int loopPos = 0;
    juce::SpinLock loopLock;
    std::atomic<bool> loopOn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PirateEQ2Processor)
};
