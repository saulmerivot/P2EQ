// Headless VST3 host for A/B tests against a reference plugin.
//   PluginAB list   <plugin.vst3>                                   -> parameters (index, name, current text)
//   PluginAB render <plugin.vst3> <in.wav> <out.wav> [name=value]... -> offline render, latency removed
// value is the plugin's own display text ("50", "Transient", "-3.0") or "n:0.25" for a normalised value.
// Name match: exact (case-insensitive) first, then substring; ambiguous/unknown names abort with the list.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

using namespace juce;

static std::unique_ptr<AudioPluginInstance> loadPlugin (const String& path, double sr, int bs)
{
    static AudioPluginFormatManager fm;   // must outlive the instances
    if (fm.getNumFormats() == 0)
        fm.addFormat (new VST3PluginFormat());
    OwnedArray<PluginDescription> types;
    for (auto* f : fm.getFormats())
        f->findAllTypesForFile (types, path);
    if (types.isEmpty()) { std::printf ("no plugin found in %s\n", path.toRawUTF8()); return nullptr; }
    String err;
    auto inst = fm.createPluginInstance (*types[0], sr, bs, err);
    if (inst == nullptr) std::printf ("load failed: %s\n", err.toRawUTF8());
    return inst;
}

static void listParams (AudioPluginInstance& p)
{
    std::printf ("%s  (latency %d)\n", p.getName().toRawUTF8(), p.getLatencySamples());
    auto& ps = p.getParameters();
    for (int i = 0; i < ps.size(); ++i)
        std::printf ("%4d  %-40s = %s\n", i, ps[i]->getName (60).toRawUTF8(), ps[i]->getCurrentValueAsText().toRawUTF8());
}

static AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    auto& ps = p.getParameters();
    for (auto* q : ps) if (q->getName (100).equalsIgnoreCase (name)) return q;
    AudioProcessorParameter* hit = nullptr;
    int hits = 0;
    for (auto* q : ps) if (q->getName (100).containsIgnoreCase (name)) { hit = q; ++hits; }
    if (hits == 1) return hit;
    std::printf ("parameter '%s' %s\n", name.toRawUTF8(), hits == 0 ? "not found" : "is ambiguous");
    return nullptr;
}

static bool applyParam (AudioPluginInstance& p, const String& assignment)
{
    const auto name = assignment.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto val  = assignment.fromFirstOccurrenceOf ("=", false, false).trim();
    auto* q = findParam (p, name);
    if (q == nullptr) return false;
    const float v = val.startsWith ("n:") ? val.substring (2).getFloatValue() : q->getValueForText (val);
    q->setValueNotifyingHost (jlimit (0.0f, 1.0f, v));
    std::printf ("  %-36s -> %s\n", q->getName (60).toRawUTF8(), q->getCurrentValueAsText().toRawUTF8());
    return true;
}

static int render (const String& pluginPath, const File& in, const File& out, const StringArray& params)
{
    AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (afm.createReaderFor (in));
    if (reader == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 2; }

    const double sr = reader->sampleRate;
    const int bs = 512, n = (int) reader->lengthInSamples;
    AudioBuffer<float> src (2, n);
    reader->read (&src, 0, n, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, n);

    auto p = loadPlugin (pluginPath, sr, bs);
    if (p == nullptr) return 3;
    p->setPlayConfigDetails (2, 2, sr, bs);
    p->setNonRealtime (true);
    for (auto& a : params)
        if (! applyParam (*p, a)) { listParams (*p); return 4; }
    p->prepareToPlay (sr, bs);
    for (auto& a : params) applyParam (*p, a);   // some plugins reset on prepare: apply again

    // let parameter smoothing settle on silence before the file starts
    AudioBuffer<float> blk (jmax (2, p->getTotalNumOutputChannels()), bs);
    MidiBuffer midi;
    for (int i = 0; i < (int) (sr / bs); ++i) { blk.clear(); p->processBlock (blk, midi); }

    const int lat = p->getLatencySamples();
    AudioBuffer<float> dst (2, n);
    dst.clear();
    for (int pos = 0; pos < n + lat; pos += bs)
    {
        blk.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const int s = pos + i;
                blk.setSample (c, i, s < n ? src.getSample (c, s) : 0.0f);
            }
        p->processBlock (blk, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const int o = pos + i - lat;
                if (o >= 0 && o < n) dst.setSample (c, o, blk.getSample (c, i));
            }
    }
    p->releaseResources();

    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<FileOutputStream> os (out.createOutputStream());
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return 5;
    os.release();
    w->writeFromAudioSampleBuffer (dst, 0, n);
    std::printf ("rendered %s  (%s, latency %d removed)\n", out.getFullPathName().toRawUTF8(), p->getName().toRawUTF8(), lat);
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && String (argv[1]) == "list")
    {
        auto p = loadPlugin (argv[2], 48000.0, 512);
        if (p == nullptr) return 3;
        listParams (*p);
        return 0;
    }
    if (argc >= 5 && String (argv[1]) == "render")
    {
        StringArray params;
        for (int i = 5; i < argc; ++i) params.add (argv[i]);
        return render (argv[2], File (argv[3]), File (argv[4]), params);
    }
    std::printf ("usage:\n  PluginAB list <plugin.vst3>\n  PluginAB render <plugin.vst3> <in.wav> <out.wav> [name=value ...]\n");
    return 1;
}
