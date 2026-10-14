// Minimal headless VST2 host for A/B tests (the VST2 SDK is no longer distributed; this declares only the
// small, publicly documented part of the binary interface that an offline render needs).
//   Vst2AB list   <plugin.dll>
//   Vst2AB render <plugin.dll> <in.wav> <out.wav> [name=value ...]
// value: display text the plugin shows ("Punish=On", "Drive=6.0", "Style=T") or "n:0.25" (normalised).
// Text values are found by scanning the parameter's display text over 0..1.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <windows.h>
#include <cstdio>

using namespace juce;

namespace vst2
{
    struct AEffect;
    using HostCallback = intptr_t (__cdecl*) (AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    using Dispatcher   = intptr_t (__cdecl*) (AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    using ProcessFn    = void (__cdecl*) (AEffect*, float** in, float** out, int32_t frames);
    using SetParamFn   = void (__cdecl*) (AEffect*, int32_t index, float value);
    using GetParamFn   = float (__cdecl*) (AEffect*, int32_t index);

    struct AEffect
    {
        int32_t magic;              // 'VstP'
        Dispatcher dispatcher;
        ProcessFn processDeprecated;
        SetParamFn setParameter;
        GetParamFn getParameter;
        int32_t numPrograms, numParams, numInputs, numOutputs, flags;
        intptr_t resvd1, resvd2;
        int32_t initialDelay, realQualities, offQualities;
        float ioRatio;
        void* object;
        void* user;
        int32_t uniqueID, version;
        ProcessFn processReplacing;
        void* processDoubleReplacing;
        char future[56];
    };

    enum : int32_t
    {
        effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
        effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetEffectName = 45,
        effStartProcess = 71, effStopProcess = 72,
        audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterCurrentId = 2, audioMasterIdle = 3,
        audioMasterGetTime = 7, audioMasterGetSampleRate = 16, audioMasterGetBlockSize = 17,
        audioMasterGetCurrentProcessLevel = 23, audioMasterGetVendorString = 32, audioMasterGetProductString = 33,
        audioMasterGetVendorVersion = 34, audioMasterCanDo = 37
    };
}

static double gSr = 48000.0;
static int gBs = 512;

static intptr_t __cdecl hostCb (vst2::AEffect*, int32_t op, int32_t, intptr_t, void* ptr, float)
{
    switch (op)
    {
        case vst2::audioMasterVersion:                return 2400;
        case vst2::audioMasterGetSampleRate:          return (intptr_t) gSr;
        case vst2::audioMasterGetBlockSize:           return gBs;
        case vst2::audioMasterGetCurrentProcessLevel: return 4;   // offline
        case vst2::audioMasterGetVendorString:        if (ptr) strcpy ((char*) ptr, "St. Punk"); return 1;
        case vst2::audioMasterGetProductString:       if (ptr) strcpy ((char*) ptr, "Vst2AB"); return 1;
        case vst2::audioMasterGetVendorVersion:       return 1000;
        default:                                      return 0;
    }
}

struct Plugin
{
    HMODULE lib = nullptr;
    vst2::AEffect* fx = nullptr;

    bool load (const String& path)
    {
        lib = LoadLibraryW (path.toWideCharPointer());
        if (lib == nullptr) { std::printf ("LoadLibrary failed (%lu)\n", GetLastError()); return false; }
        using Main = vst2::AEffect* (__cdecl*) (vst2::HostCallback);
        auto entry = (Main) GetProcAddress (lib, "VSTPluginMain");
        if (entry == nullptr) entry = (Main) GetProcAddress (lib, "main");
        if (entry == nullptr) { std::printf ("no VST2 entry point\n"); return false; }
        fx = entry (hostCb);
        if (fx == nullptr || fx->magic != 0x56737450) { std::printf ("not a VST2 effect\n"); return false; }
        call (vst2::effOpen);
        return true;
    }
    intptr_t call (int32_t op, int32_t idx = 0, intptr_t val = 0, void* ptr = nullptr, float opt = 0)
    {
        return fx->dispatcher (fx, op, idx, val, ptr, opt);
    }
    String str (int32_t op, int idx)
    {
        char buf[256] = {};
        call (op, idx, 0, buf);
        return String (buf).trim();
    }
    String name()               { return str (vst2::effGetEffectName, 0); }
    String paramName (int i)    { return str (vst2::effGetParamName, i); }
    String paramText (int i)    { return (str (vst2::effGetParamDisplay, i) + " " + str (vst2::effGetParamLabel, i)).trim(); }
    ~Plugin()
    {
        if (fx) { call (vst2::effMainsChanged, 0, 0); call (vst2::effClose); }
        if (lib) FreeLibrary (lib);
    }
};

static void listParams (Plugin& p)
{
    std::printf ("%s  (%d params, %d in / %d out, delay %d)\n", p.name().toRawUTF8(), p.fx->numParams,
                 p.fx->numInputs, p.fx->numOutputs, p.fx->initialDelay);
    for (int i = 0; i < p.fx->numParams; ++i)
        std::printf ("%4d  %-28s = %-16s (n %.3f)\n", i, p.paramName (i).toRawUTF8(), p.paramText (i).toRawUTF8(),
                     p.fx->getParameter (p.fx, i));
}

static bool applyParam (Plugin& p, const String& assignment)
{
    const auto name = assignment.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto val  = assignment.fromFirstOccurrenceOf ("=", false, false).trim();
    int idx = -1, hits = 0;
    for (int i = 0; i < p.fx->numParams; ++i) if (p.paramName (i).equalsIgnoreCase (name)) idx = i;
    if (idx < 0)
        for (int i = 0; i < p.fx->numParams; ++i) if (p.paramName (i).containsIgnoreCase (name)) { idx = i; ++hits; }
    if (idx < 0 || hits > 1) { std::printf ("parameter '%s' %s\n", name.toRawUTF8(), idx < 0 ? "not found" : "ambiguous"); return false; }

    float v = -1.0f;
    if (val.startsWith ("n:")) v = val.substring (2).getFloatValue();
    else
    {
        // scan display texts: exact text match wins, otherwise closest number
        const bool numeric = val.retainCharacters ("-0123456789.").isNotEmpty() && ! val.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        const float target = val.getFloatValue();
        float bestErr = 1e30f;
        for (int k = 0; k <= 1000; ++k)
        {
            const float n = (float) k / 1000.0f;
            p.fx->setParameter (p.fx, idx, n);
            const auto shown = p.str (vst2::effGetParamDisplay, idx);
            if (! numeric)
            {
                if (shown.equalsIgnoreCase (val)) { v = n; break; }
                continue;
            }
            const float err = std::abs (shown.retainCharacters ("-0123456789.").getFloatValue() - target);
            if (err < bestErr) { bestErr = err; v = n; }
        }
    }
    if (v < 0) { std::printf ("value '%s' not found for %s\n", val.toRawUTF8(), name.toRawUTF8()); return false; }
    p.fx->setParameter (p.fx, idx, jlimit (0.0f, 1.0f, v));
    std::printf ("  %-24s -> %s\n", p.paramName (idx).toRawUTF8(), p.paramText (idx).toRawUTF8());
    return true;
}

static int render (const String& path, const File& in, const File& out, const StringArray& params)
{
    AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (afm.createReaderFor (in));
    if (reader == nullptr) { std::printf ("cannot read input\n"); return 2; }
    gSr = reader->sampleRate;
    const int n = (int) reader->lengthInSamples;
    AudioBuffer<float> src (2, n);
    reader->read (&src, 0, n, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, n);

    // 1) resolve each value on a probe instance (scanning display texts sweeps the parameter)
    std::vector<std::pair<int, float>> resolved;
    {
        Plugin probe;
        if (! probe.load (path)) return 3;
        probe.call (vst2::effSetSampleRate, 0, 0, nullptr, (float) gSr);
        probe.call (vst2::effSetBlockSize, 0, gBs);
        for (auto& a : params)
        {
            if (! applyParam (probe, a)) { listParams (probe); return 4; }
            const auto name = a.upToFirstOccurrenceOf ("=", false, false).trim();
            int idx = -1;
            for (int i = 0; i < probe.fx->numParams && idx < 0; ++i) if (probe.paramName (i).equalsIgnoreCase (name)) idx = i;
            for (int i = 0; i < probe.fx->numParams && idx < 0; ++i) if (probe.paramName (i).containsIgnoreCase (name)) idx = i;
            resolved.push_back ({ idx, probe.fx->getParameter (probe.fx, idx) });
        }
    }
    // 2) fresh instance, activated first, then each parameter set exactly once
    //    (some plugins pass audio through untouched if a parameter was swept while inactive)
    Plugin p;
    if (! p.load (path)) return 3;
    p.call (vst2::effSetSampleRate, 0, 0, nullptr, (float) gSr);
    p.call (vst2::effSetBlockSize, 0, gBs);
    p.call (vst2::effMainsChanged, 0, 1);
    p.call (vst2::effStartProcess);
    for (auto& [i, v] : resolved) p.fx->setParameter (p.fx, i, v);

    const int nin = juce::jmax (2, p.fx->numInputs), nout = juce::jmax (2, p.fx->numOutputs);
    AudioBuffer<float> ib (nin, gBs), ob (nout, gBs);
    // settle on silence (smoothing, auto-gain) for 1 s
    for (int i = 0; i < (int) (2.0 * gSr / gBs); ++i)
    {
        p.call (19);   // effEditIdle  - some plugins apply parameter/style changes on the host idle tick
        p.call (53);   // effIdle (legacy)
        MSG msg;       // ...and some post them through the Windows message queue
        while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&msg); DispatchMessageW (&msg); }
        if (i % 8 == 0) Sleep (1);
        ib.clear(); ob.clear();
        p.fx->processReplacing (p.fx, const_cast<float**> (ib.getArrayOfWritePointers()), const_cast<float**> (ob.getArrayOfWritePointers()), gBs);
    }
    const int lat = p.fx->initialDelay;
    AudioBuffer<float> dst (2, n);
    dst.clear();
    for (int pos = 0; pos < n + lat; pos += gBs)
    {
        ib.clear(); ob.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < gBs; ++i)
                if (pos + i < n) ib.setSample (c, i, src.getSample (c, pos + i));
        p.fx->processReplacing (p.fx, const_cast<float**> (ib.getArrayOfWritePointers()), const_cast<float**> (ob.getArrayOfWritePointers()), gBs);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < gBs; ++i)
            {
                const int o = pos + i - lat;
                if (o >= 0 && o < n) dst.setSample (c, o, ob.getSample (c, i));
            }
    }
    p.call (vst2::effStopProcess);

    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<FileOutputStream> os (out.createOutputStream());
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), gSr, 2, 24, {}, 0));
    if (w == nullptr) return 5;
    os.release();
    w->writeFromAudioSampleBuffer (dst, 0, n);
    std::printf ("rendered %s  (%s, latency %d removed)\n", out.getFullPathName().toRawUTF8(), p.name().toRawUTF8(), lat);
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && String (argv[1]) == "list")
    {
        Plugin p;
        if (! p.load (argv[2])) return 3;
        listParams (p);
        return 0;
    }
    if (argc >= 5 && String (argv[1]) == "render")
    {
        StringArray params;
        for (int i = 5; i < argc; ++i) params.add (argv[i]);
        return render (argv[2], File (argv[3]), File (argv[4]), params);
    }
    std::printf ("usage:\n  Vst2AB list <plugin.dll>\n  Vst2AB render <plugin.dll> <in.wav> <out.wav> [name=value ...]\n");
    return 1;
}
