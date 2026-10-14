// Renders the plugin editor to PNG files (offscreen) for layout checks.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // CPU benchmark: EditorShot bench <blockSize> <satOn 0/1>
    if (argc > 1 && juce::String (argv[1]) == "bench")
    {
        const int bs = argc > 2 ? std::atoi (argv[2]) : 128;
        const bool satOn = argc > 3 && std::atoi (argv[3]) != 0;
        const double sr = 44100.0;
        PirateEQ2Processor proc;
        if (auto* p = proc.apvts.getParameter ("sat_on")) p->setValueNotifyingHost (satOn ? 1.0f : 0.0f);
        proc.prepareToPlay (sr, bs);
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        const int blocks = (int) (20.0 * sr / bs);
        double total = 0, worst = 0;
        std::vector<double> times;
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < bs; ++i) buf.setSample (c, i, (rng.nextFloat() - 0.5f) * 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks();
            proc.processBlock (buf, midi);
            const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;
            if (b > 50) { total += ms; worst = std::max (worst, ms); times.push_back (ms); }
        }
        std::sort (times.begin(), times.end());
        const double budget = 1000.0 * bs / sr;
        const int over = (int) std::count_if (times.begin(), times.end(), [&] (double t) { return t > budget * 0.7; });
        std::printf ("block %d (budget %.2f ms) sat %s: avg %.3f ms (%.0f%% CPU) | p99 %.3f ms | worst %.3f ms | blocks >70%% budget: %d of %d\n",
                     bs, budget, satOn ? "on" : "off", total / (double) times.size(), 100.0 * total / (double) times.size() / budget,
                     times[(size_t) (times.size() * 0.99)], worst, over, (int) times.size());
        return 0;
    }

    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    const int w = argc > 2 ? std::atoi (argv[2]) : 1120;
    const int h = argc > 3 ? std::atoi (argv[3]) : 860;

    PirateEQ2Processor proc;
    if (argc > 4) proc.apvts.state.setProperty ("uiMode", std::atoi (argv[4]), nullptr);
    if (auto* sOn = proc.apvts.getParameter ("sat_on")) sOn->setValueNotifyingHost (1.0f);   // show SAT lit in renders
    proc.prepareToPlay (48000.0, 512);

    auto setP = [&] (const char* id, float v)
    {
        if (auto* q = proc.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
    };
    setP ("b2_gainT", 9.0f);  setP ("b2_gainN", 0.0f);     // split band: Transient +9 dB only
    setP ("b4_gainT", -5.0f); setP ("b4_gainN", -5.0f);    // linked band (shows pull tabs)
    setP ("b6_gainN", 6.0f);                                // Tonal-only boost

    // feed some audio so the analyzer has content
    juce::AudioBuffer<float> buf (2, 512);
    juce::Random rng (7);
    for (int blk = 0; blk < 120; ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const int t = blk * 512 + i;
            const float env = std::exp (-(float) (t % 6000) / 700.0f);
            const float v = 0.15f * std::sin (0.05f * (float) t) + 0.12f * std::sin (0.21f * (float) t)
                          + (rng.nextFloat() - 0.5f) * 0.9f * env;
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        juce::MidiBuffer m;
        proc.processBlock (buf, m);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (w, h);

    for (int i = 0; i < 3; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds());
    auto f = outDir.getChildFile (argc > 4 && std::atoi (argv[4]) == 1 ? "editor_old.png" : "editor.png");
    f.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = f.createOutputStream())
        png.writeImageToStream (img, *os);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
