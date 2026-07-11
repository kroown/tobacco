using System;

namespace Tobacco.Audio;

public class Lfo
{
    public WaveformType Waveform { get; set; } = WaveformType.Sine;
    public double Rate { get; set; } = 1.0;
    public double Depth { get; set; } = 1.0;
    public double PhaseOffset { get; set; } = 0.0;

    private double _phase;

    public double Next(double dt)
    {
        _phase += Rate * dt;
        _phase %= 1.0;
        if (_phase < 0) _phase += 1.0;
        return Oscillators.Generate(Waveform, _phase + PhaseOffset) * Depth;
    }

    public void Reset() { _phase = 0.0; }
}
