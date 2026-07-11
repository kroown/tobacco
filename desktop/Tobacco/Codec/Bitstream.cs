using System;

namespace Tobacco.Codec;

public class BitWriter
{
    private byte[] _data;
    private int _bytePos;
    private int _bitPos;

    public BitWriter(int capacity)
    {
        _data = new byte[capacity];
        _bytePos = 0;
        _bitPos = 0;
    }

    public void Write(uint value, int nbits)
    {
        for (int i = nbits - 1; i >= 0; i--)
        {
            if (_bytePos >= _data.Length)
                Array.Resize(ref _data, _data.Length * 2);
            _data[_bytePos] |= (byte)(((value >> i) & 1) << (7 - _bitPos));
            _bitPos++;
            if (_bitPos >= 8)
            {
                _bitPos = 0;
                _bytePos++;
            }
        }
    }

    public int Flush()
    {
        if (_bitPos > 0) _bytePos++;
        return _bytePos;
    }

    public byte[] GetBuffer() => _data;
    public int Position => _bytePos * 8 + _bitPos;
}

public class BitReader
{
    private readonly byte[] _data;
    private readonly int _len;
    private int _bytePos;
    private int _bitPos;

    public BitReader(byte[] data, int len)
    {
        _data = data;
        _len = len;
        _bytePos = 0;
        _bitPos = 0;
    }

    public uint Read(int nbits)
    {
        uint val = 0;
        for (int i = 0; i < nbits; i++)
        {
            if (_bytePos >= _len) return val;
            val <<= 1;
            val |= (uint)((_data[_bytePos] >> (7 - _bitPos)) & 1);
            _bitPos++;
            if (_bitPos >= 8)
            {
                _bitPos = 0;
                _bytePos++;
            }
        }
        return val;
    }

    public int Position => _bytePos * 8 + _bitPos;
}
