namespace Tobacco.Audio;

public enum FilterType { Lowpass, Highpass, Bandpass, Notch }

public class AudioFilter
{
    public FilterType Type { get; set; } = FilterType.Lowpass;
    public double Frequency { get; set; } = 1000.0;
    public double Resonance { get; set; } = 1.0;

    private double _y1, _y2, _x1, _x2;

    public double Process(double input, double sampleRate)
    {
        double w0 = 2.0 * System.Math.PI * Frequency / sampleRate;
        double sinW = System.Math.Sin(w0);
        double cosW = System.Math.Cos(w0);
        double alpha = sinW / (2.0 * Resonance);

        double b0, b1, b2, a0, a1, a2;
        switch (Type)
        {
            case FilterType.Lowpass:
                b0 = (1.0 - cosW) / 2.0; b1 = 1.0 - cosW; b2 = (1.0 - cosW) / 2.0;
                a0 = 1.0 + alpha; a1 = -2.0 * cosW; a2 = 1.0 - alpha;
                break;
            case FilterType.Highpass:
                b0 = (1.0 + cosW) / 2.0; b1 = -(1.0 + cosW); b2 = (1.0 + cosW) / 2.0;
                a0 = 1.0 + alpha; a1 = -2.0 * cosW; a2 = 1.0 - alpha;
                break;
            case FilterType.Bandpass:
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1.0 + alpha; a1 = -2.0 * cosW; a2 = 1.0 - alpha;
                break;
            default:
                b0 = 1.0; b1 = 0; b2 = 0; a0 = 1.0; a1 = 0; a2 = 0;
                break;
        }

        double output = (b0 / a0) * input + (b1 / a0) * _x1 + (b2 / a0) * _x2
                        - (a1 / a0) * _y1 - (a2 / a0) * _y2;
        _x2 = _x1; _x1 = input;
        _y2 = _y1; _y1 = output;
        return output;
    }

    public void Reset()
    {
        _y1 = _y2 = _x1 = _x2 = 0;
    }
}
