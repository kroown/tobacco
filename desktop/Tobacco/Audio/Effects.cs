using System;

namespace Tobacco.Audio;

public class DelayEffect
{
    public double Time { get; set; } = 0.3;
    public double Feedback { get; set; } = 0.4;
    public double Mix { get; set; } = 0.3;
    private double[] _buffer;
    private int _pos;
    private int _capacity;

    public void Init(double sampleRate)
    {
        _capacity = (int)(sampleRate * 2.0);
        _buffer = new double[_capacity];
        _pos = 0;
    }

    public double Process(double input, double sampleRate)
    {
        if (_buffer == null) Init(sampleRate);
        int delay = (int)(Time * sampleRate);
        if (delay >= _capacity) delay = _capacity - 1;
        int readPos = (_pos - delay + _capacity) % _capacity;
        double delayed = _buffer[readPos];
        _buffer[_pos] = input + delayed * Feedback;
        _pos = (_pos + 1) % _capacity;
        return input * (1.0 - Mix) + delayed * Mix;
    }

    public void Reset()
    {
        if (_buffer != null) Array.Clear(_buffer);
        _pos = 0;
    }
}

public class ReverbEffect
{
    public double RoomSize { get; set; } = 0.5;
    public double Damping { get; set; } = 0.5;
    public double Mix { get; set; } = 0.3;
    private double[] _comb1, _comb2, _comb3, _comb4;
    private double[] _allpass;
    private int _p1, _p2, _p3, _p4, _pa;

    public void Init(double sampleRate)
    {
        _comb1 = new double[(int)(sampleRate * 0.0297)];
        _comb2 = new double[(int)(sampleRate * 0.0371)];
        _comb3 = new double[(int)(sampleRate * 0.0411)];
        _comb4 = new double[(int)(sampleRate * 0.0437)];
        _allpass = new double[(int)(sampleRate * 0.005)];
    }

    public double Process(double input, double sampleRate)
    {
        if (_comb1 == null) Init(sampleRate);
        double out1 = Comb(input, _comb1, ref _p1, RoomSize, Damping);
        double out2 = Comb(input, _comb2, ref _p2, RoomSize, Damping);
        double out3 = Comb(input, _comb3, ref _p3, RoomSize, Damping);
        double out4 = Comb(input, _comb4, ref _p4, RoomSize, Damping);
        double sum = (out1 + out2 + out3 + out4) / 4.0;
        double ap = AllPass(sum, _allpass, ref _pa);
        return input * (1.0 - Mix) + ap * Mix;
    }

    private static double Comb(double input, double[] buf, ref int pos, double volume, double damping)
    {
        double output = buf[pos];
        buf[pos] = input + output * volume * (1.0 - damping) + buf[pos] * damping * volume;
        pos = (pos + 1) % buf.Length;
        return output;
    }

    private static double AllPass(double input, double[] buf, ref int pos)
    {
        double output = buf[pos];
        buf[pos] = input + output * 0.5;
        pos = (pos + 1) % buf.Length;
        return output - buf[pos];
    }

    public void Reset()
    {
        if (_comb1 != null) Array.Clear(_comb1);
        if (_comb2 != null) Array.Clear(_comb2);
        if (_comb3 != null) Array.Clear(_comb3);
        if (_comb4 != null) Array.Clear(_comb4);
        if (_allpass != null) Array.Clear(_allpass);
        _p1 = _p2 = _p3 = _p4 = _pa = 0;
    }
}

public class ChorusEffect
{
    public double Rate { get; set; } = 1.5;
    public double Depth { get; set; } = 0.002;
    public double Mix { get; set; } = 0.4;
    private double[] _buffer;
    private int _pos;
    private double _phase;
    private int _capacity;

    public void Init(double sampleRate)
    {
        _capacity = (int)(sampleRate * 0.1);
        _buffer = new double[_capacity];
    }

    public double Process(double input, double sampleRate, double dt)
    {
        if (_buffer == null) Init(sampleRate);
        _buffer[_pos] = input;
        double mod = Math.Sin(2.0 * Math.PI * Rate * _phase) * Depth * sampleRate;
        _phase += dt;
        int readPos = (_pos - (int)mod - 1 + _capacity * 2) % _capacity;
        double delayed = _buffer[readPos];
        _pos = (_pos + 1) % _capacity;
        return input * (1.0 - Mix) + delayed * Mix;
    }

    public void Reset()
    {
        if (_buffer != null) Array.Clear(_buffer);
        _pos = 0; _phase = 0;
    }
}
