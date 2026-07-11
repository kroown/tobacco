using System;
using System.Collections.Generic;

namespace Tobacco.Audio;

public class SynthNote
{
    public double Frequency { get; set; } = 440.0;
    public double Velocity { get; set; } = 1.0;
    public bool Active { get; set; }
    public bool Released { get; set; }
    public double Phase { get; set; }
    public AdsrEnvelope Envelope { get; } = new AdsrEnvelope();
}

public class SynthEngine
{
    public double SampleRate { get; set; } = 44100.0;
    public WaveformType Waveform { get; set; } = WaveformType.Sine;
    public int Polyphony { get; set; } = 8;

    public AdsrEnvelope AdsrTemplate { get; set; } = new AdsrEnvelope
    {
        AttackTime = 0.01, DecayTime = 0.1, SustainLevel = 0.7, ReleaseTime = 0.2
    };

    public AudioFilter Filter { get; set; } = new AudioFilter { Type = FilterType.Lowpass, Frequency = 5000.0, Resonance = 1.0 };
    public Lfo FilterLfo { get; set; } = new Lfo { Rate = 0.5, Depth = 500.0 };
    public Lfo PitchLfo { get; set; } = new Lfo { Rate = 0.0, Depth = 0.0 };
    public DelayEffect Delay { get; set; } = new DelayEffect();
    public ReverbEffect Reverb { get; set; } = new ReverbEffect();
    public ChorusEffect Chorus { get; set; } = new ChorusEffect();

    public double MasterVolume { get; set; } = 0.8;
    public bool DelayEnabled { get; set; } = false;
    public bool ReverbEnabled { get; set; } = false;
    public bool ChorusEnabled { get; set; } = false;
    public bool FilterEnabled { get; set; } = false;

    private List<SynthNote> _notes = new();
    private double _dt;

    public void Init()
    {
        _dt = 1.0 / SampleRate;
        Delay.Init(SampleRate);
        Reverb.Init(SampleRate);
        Chorus.Init(SampleRate);
    }

    public void NoteOn(double frequency, double velocity = 1.0)
    {
        for (int i = 0; i < Polyphony; i++)
        {
            if (i < _notes.Count && !_notes[i].Active && !_notes[i].Released)
            {
                _notes[i].Frequency = frequency;
                _notes[i].Velocity = velocity;
                _notes[i].Active = true;
                _notes[i].Released = false;
                _notes[i].Phase = 0.0;
                _notes[i].Envelope.AttackTime = AdsrTemplate.AttackTime;
                _notes[i].Envelope.DecayTime = AdsrTemplate.DecayTime;
                _notes[i].Envelope.SustainLevel = AdsrTemplate.SustainLevel;
                _notes[i].Envelope.ReleaseTime = AdsrTemplate.ReleaseTime;
                _notes[i].Envelope.NoteOn();
                return;
            }
        }
        if (_notes.Count < Polyphony)
        {
            var note = new SynthNote
            {
                Frequency = frequency,
                Velocity = velocity,
                Active = true,
                Released = false,
                Phase = 0.0
            };
            note.Envelope.AttackTime = AdsrTemplate.AttackTime;
            note.Envelope.DecayTime = AdsrTemplate.DecayTime;
            note.Envelope.SustainLevel = AdsrTemplate.SustainLevel;
            note.Envelope.ReleaseTime = AdsrTemplate.ReleaseTime;
            note.Envelope.NoteOn();
            _notes.Add(note);
        }
    }

    public void NoteOff(double frequency)
    {
        foreach (var note in _notes)
        {
            if (note.Active && note.Frequency == frequency)
            {
                note.Envelope.NoteOff();
                note.Released = true;
            }
        }
    }

    public void AllNotesOff()
    {
        foreach (var note in _notes)
        {
            note.Envelope.NoteOff();
            note.Released = true;
        }
    }

    public double NextSample()
    {
        double mix = 0;
        double filterFreq = Filter.Frequency + FilterLfo.Next(_dt);
        if (filterFreq < 20) filterFreq = 20;
        if (filterFreq > SampleRate / 2) filterFreq = SampleRate / 2;
        Filter.Frequency = filterFreq;
        double pitchMod = PitchLfo.Next(_dt);

        foreach (var note in _notes)
        {
            if (!note.Active) continue;
            double env = note.Envelope.Next(_dt);
            if (!note.Envelope.IsActive && note.Released)
            {
                note.Active = false;
                continue;
            }
            double phaseInc = (note.Frequency + pitchMod) / SampleRate;
            note.Phase += phaseInc;
            if (note.Phase >= 1.0) note.Phase -= 1.0;
            double osc = Oscillators.Generate(Waveform, note.Phase);
            double sample = osc * env * note.Velocity;
            if (FilterEnabled)
                sample = Filter.Process(sample, SampleRate);
            mix += sample;
        }

        mix *= MasterVolume;
        if (ChorusEnabled)
            mix = Chorus.Process(mix, SampleRate, _dt);
        if (DelayEnabled)
            mix = Delay.Process(mix, SampleRate);
        if (ReverbEnabled)
            mix = Reverb.Process(mix, SampleRate);

        if (mix > 1.0) mix = 1.0;
        if (mix < -1.0) mix = -1.0;
        return mix;
    }

    public short[] RenderBuffer(int numSamples, int channels)
    {
        short[] buf = new short[numSamples * channels];
        for (int i = 0; i < numSamples; i++)
        {
            double sample = NextSample();
            short s = (short)(sample * 32767.0);
            for (int ch = 0; ch < channels; ch++)
                buf[i * channels + ch] = s;
        }
        return buf;
    }
}
