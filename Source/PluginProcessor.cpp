#include "PluginProcessor.h"
#include "PluginEditor.h"

PirateEQ2Processor::PirateEQ2Processor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", Params::createLayout())
{
}

void PirateEQ2Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    split.prepare (sampleRate);
    for (auto& e : eq) e.prepare (sampleRate);

    gainT.reset (sampleRate, 0.02);
    gainN.reset (sampleRate, 0.02);
    gainT.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (Params::raw (apvts, "out_gainT")));
    gainN.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (Params::raw (apvts, "out_gainN")));

    for (auto& st : sat) st.prepare (sampleRate, samplesPerBlock);
    trBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    tnBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    setLatencySamples (TransientSplit::latency + sat[0].latency());

    // re-resample the test loop to the real device rate
    if (loopOn.load() && testLoopFile.existsAsFile())
        loadTestLoop (testLoopFile);
}

bool PirateEQ2Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && l.getMainInputChannelSet() == out;
}

void PirateEQ2Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = std::min (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();
    const bool stereo = numCh == 2;

    // --- read parameters once per block
    const float sep = Params::raw (apvts, "split_sep") * 0.01f;
    const float decay = Params::raw (apvts, "split_decay") * 0.01f;
    const float smooth = Params::raw (apvts, "split_smooth");
    const int source = (int) Params::raw (apvts, "split_source");
    const int solo = (int) Params::raw (apvts, "split_solo");
    split.setSource (source);
    split.setParams (sep, decay, smooth);

    const bool bypass = Params::raw (apvts, "bypass") > 0.5f;
    const bool eqBypass = Params::raw (apvts, "eq_bypass") > 0.5f;
    const float phase = Params::raw (apvts, "phase_inv") > 0.5f ? -1.0f : 1.0f;
    const int outMode = (int) Params::raw (apvts, "out_mode");
    const double outPan[2] = { Params::raw (apvts, "out_panT") * 0.01, Params::raw (apvts, "out_panN") * 0.01 };
    gainT.setTargetValue (juce::Decibels::decibelsToGain (Params::raw (apvts, "out_gainT")));
    gainN.setTargetValue (juce::Decibels::decibelsToGain (Params::raw (apvts, "out_gainN")));

    const double scale[2] = { Params::raw (apvts, "scaleT") * 0.01, Params::raw (apvts, "scaleN") * 0.01 };
    for (int st = 0; st < 2; ++st)
    {
        std::array<BandTarget, StreamEQ::numBands> t;
        for (int b = 0; b < StreamEQ::numBands; ++b)
            t[(size_t) b] = Params::readBand (apvts, b, st, scale[st]);
        eq[(size_t) st].setTargets (t);
    }

    // band solo: audition one band's region (band-pass at its freq/Q) on both streams
    int soloBand = -1;
    for (int b = 0; b < StreamEQ::numBands; ++b)
        if (Params::raw (apvts, Params::id (b, "solo")) > 0.5f) { soloBand = b; break; }
    if (soloBand >= 0)
    {
        const bool edge = soloBand == 0 || soloBand == 7;
        for (int st = 0; st < 2; ++st)
        {
            const double f = edge ? Params::raw (apvts, Params::id (soloBand, st == 0 ? "freqT" : "freqN"))
                                  : Params::raw (apvts, Params::id (soloBand, "freq"));
            const double q = edge ? 0.7 : Params::raw (apvts, Params::id (soloBand, st == 0 ? "qT" : "qN"));
            BandFilter::rbj (soloBp[st], BandType::BandPass, currentSampleRate.load(), f, 0.0, q);
        }
    }

    float pk[2] { 0, 0 };
    double sq[2] { 0, 0 };
    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = stereo ? buffer.getWritePointer (1) : nullptr;

    if (loopOn.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (loopLock);
        if (lock.isLocked() && loopBuf.getNumSamples() > 0)
        {
            const int len = loopBuf.getNumSamples();
            const float* lsrc = loopBuf.getReadPointer (0);
            const float* rsrc = loopBuf.getReadPointer (loopBuf.getNumChannels() > 1 ? 1 : 0);
            for (int i = 0; i < n; ++i)
            {
                ch0[i] = lsrc[loopPos];
                if (stereo) ch1[i] = rsrc[loopPos];
                if (++loopPos >= len) loopPos = 0;
            }
        }
    }

    // ---------------------------------------------------------------- saturation settings
    const bool satOn = Params::raw (apvts, "sat_on") > 0.5f;
    const int satTarget = (int) Params::raw (apvts, "sat_target");          // 0 both, 1 transient, 2 tonal
    {
        Saturator::Settings ss;
        ss.style = (int) Params::raw (apvts, "sat_style");
        // measured on the reference: 3.3 dB per Drive step from -2 dB, Punish adds ~14 dB
        const float driveDb = Params::raw (apvts, "sat_drive") * 3.3f - 2.0f + (Params::raw (apvts, "sat_punish") > 0.5f ? 14.0f : 0.0f);
        ss.driveDb = driveDb;
        ss.toneDb = Params::raw (apvts, "sat_tone") * 0.12f;               // -100..100 % -> -12..+12 (reference scale)
        ss.lowCutHz = Params::raw (apvts, "sat_lowcut");
        ss.thump = Params::raw (apvts, "sat_thump") > 0.5f;
        ss.highCutHz = Params::raw (apvts, "sat_highcut");
        ss.steep = Params::raw (apvts, "sat_steep") > 0.5f;
        static constexpr float autoFactor[5] = { 0.10f, 0.42f, 0.55f, 0.55f, 0.55f };   // per style, fitted
        ss.outDb = Params::raw (apvts, "sat_output")
                   - (Params::raw (apvts, "sat_auto") > 0.5f ? std::max (0.0f, driveDb) * autoFactor[juce::jlimit (0, 4, ss.style)] : 0.0f);
        ss.mix = Params::raw (apvts, "sat_mix") * 0.01f;
        for (auto& st : sat) st.setSettings (ss);
    }

    if (trBuf.getNumSamples() < n)
    {
        trBuf.setSize (2, n, false, false, true);
        tnBuf.setSize (2, n, false, false, true);
    }
    float* tr0 = trBuf.getWritePointer (0); float* tr1 = trBuf.getWritePointer (1);
    float* tn0 = tnBuf.getWritePointer (0); float* tn1 = tnBuf.getWritePointer (1);

    // ---------------------------------------------------------------- 1) split + EQ (or bypass / band solo)
    const bool normal = ! bypass && soloBand < 0;
    constexpr int sub = 32;
    for (int pos = 0; pos < n; pos += sub)
    {
        const int len = std::min (sub, n - pos);
        if (normal && ! eqBypass) { eq[0].step(); eq[1].step(); }

        for (int i = pos; i < pos + len; ++i)
        {
            float tr[2] { 0, 0 }, tn[2] { 0, 0 };
            split.process (ch0[i], stereo ? ch1[i] : ch0[i], tr[0], tn[0], tr[1], tn[1]);

            if (soloBand >= 0 && ! bypass)
            {
                for (int c = 0; c < numCh; ++c)
                {
                    tr[c] = (float) (soloBp[0].process (c, tr[c]) + soloBp[1].process (c, tn[c]));
                    tn[c] = 0.0f;
                }
            }
            else if (normal && ! eqBypass)
            {
                eq[0].process (tr[0], tr[1], stereo);
                eq[1].process (tn[0], tn[1], stereo);
            }
            tr0[i] = tr[0]; tr1[i] = tr[1];
            tn0[i] = tn[0]; tn1[i] = tn[1];
        }
    }

    // ---------------------------------------------------------------- 2) saturation (block, oversampled)
    // Both streams always take the same path (oversampled or plain delay) so they stay phase-aligned.
    sat[0].process (trBuf, numCh, n, satOn, satOn && normal && satTarget != 2);
    sat[1].process (tnBuf, numCh, n, satOn, satOn && normal && satTarget != 1);

    // ---------------------------------------------------------------- 3) master, meters, analyzer, sum
    for (int i = 0; i < n; ++i)
    {
        if (! normal)
        {
            ch0[i] = tr0[i] + tn0[i];
            if (stereo) ch1[i] = tr1[i] + tn1[i];
            continue;
        }

        // analyzer feed (post-EQ/SAT, mono mix)
        const int w = anWrite.load (std::memory_order_relaxed);
        anBuf[0][(size_t) (w & (anSize - 1))] = stereo ? 0.5f * (tr0[i] + tr1[i]) : tr0[i];
        anBuf[1][(size_t) (w & (anSize - 1))] = stereo ? 0.5f * (tn0[i] + tn1[i]) : tn0[i];
        anWrite.store (w + 1, std::memory_order_release);

        const float gT = gainT.getNextValue(), gN = gainN.getNextValue();

        // master: pan / M-S per stream, then gain
        double t0 = tr0[i], t1 = tr1[i], n0 = tn0[i], n1 = tn1[i];
        if (stereo)
        {
            routePair (t0, t1, outMode, outPan[0]);
            routePair (n0, n1, outMode, outPan[1]);
        }
        t0 *= gT; t1 *= gT; n0 *= gN; n1 *= gN;

        for (int c = 0; c < numCh; ++c)
        {
            const double a = c == 0 ? t0 : t1, b = c == 0 ? n0 : n1;
            pk[0] = std::max (pk[0], (float) std::abs (a));
            pk[1] = std::max (pk[1], (float) std::abs (b));
            sq[0] += a * a;
            sq[1] += b * b;
        }

        double o0, o1;
        if (solo == 1)      { o0 = t0;      o1 = t1; }
        else if (solo == 2) { o0 = n0;      o1 = n1; }
        else                { o0 = t0 + n0; o1 = t1 + n1; }

        ch0[i] = (float) o0 * phase;
        if (stereo) ch1[i] = (float) o1 * phase;
    }

    const double cnt = std::max (1, n * numCh);
    for (int s = 0; s < 2; ++s)
    {
        peak[s].store (pk[s]);
        rms[s].store ((float) std::sqrt (sq[s] / cnt));
    }
}

bool PirateEQ2Processor::loadTestLoop (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;

    const int ch = (int) std::min (2u, reader->numChannels);
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> src (ch, n);
    reader->read (&src, 0, n, 0, true, ch > 1);

    // linear resample to the running sample rate
    const double ratio = reader->sampleRate / std::max (1.0, currentSampleRate.load());
    const int outLen = std::max (1, (int) (n / ratio));
    juce::AudioBuffer<float> out (ch, outLen);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = src.getReadPointer (c);
        float* d = out.getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
        {
            const double p = i * ratio;
            const int i0 = std::min ((int) p, n - 1), i1 = std::min (i0 + 1, n - 1);
            const float fr = (float) (p - i0);
            d[i] = s[i0] + (s[i1] - s[i0]) * fr;
        }
    }

    {
        const juce::SpinLock::ScopedLockType lock (loopLock);
        loopBuf = std::move (out);
        loopPos = 0;
    }
    testLoopName = f.getFileName();
    testLoopFile = f;
    loopOn = true;
    return true;
}

juce::AudioProcessorEditor* PirateEQ2Processor::createEditor()
{
    return new PirateEQ2Editor (*this);
}

void PirateEQ2Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PirateEQ2Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PirateEQ2Processor();
}
