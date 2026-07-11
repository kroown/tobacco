using System;

namespace Tobacco.Audio;

public enum WaveformType { Sine, Square, Saw, Triangle, Noise }

public static class Oscillators
{
    private static Random _rng = new Random();

    public static double Generate(WaveformType type, double phase)
    {
        double t = phase % 1.0;
        if (t < 0) t += 1.0;
        return type switch
        {
            WaveformType.Sine => Math.Sin(2.0 * Math.PI * t),
            WaveformType.Square => t < 0.5 ? 1.0 : -1.0,
            WaveformType.Saw => 2.0 * t - 1.0,
            WaveformType.Triangle => t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t,
            WaveformType.Noise => 2.0 * _rng.NextDouble() - 1.0,
            _ => 0.0
        };
    }
}
