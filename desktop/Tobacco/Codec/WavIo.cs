using System;
using System.IO;
using System.Text;

namespace Tobacco.Codec;

public static class WavIo
{
    public struct WavInfo
    {
        public int Channels;
        public int SampleRate;
        public int Bits;
        public int DataOffset;
        public int DataSize;
    }

    public static WavInfo ReadHeader(string path)
    {
        using var fp = new FileStream(path, FileMode.Open, FileAccess.Read);
        byte[] hdr = new byte[44];
        if (fp.Read(hdr, 0, 44) != 44)
            throw new InvalidDataException("Failed to read WAV header");
        if (Encoding.ASCII.GetString(hdr, 0, 4) != "RIFF" || Encoding.ASCII.GetString(hdr, 8, 4) != "WAVE")
            throw new InvalidDataException("Not a WAV file");

        var info = new WavInfo();
        info.Channels = BitConverter.ToUInt16(hdr, 22);
        info.SampleRate = BitConverter.ToInt32(hdr, 24);
        info.Bits = BitConverter.ToUInt16(hdr, 34);
        info.DataOffset = 44;
        info.DataSize = BitConverter.ToInt32(hdr, 40);

        while (true)
        {
            byte[] tag = new byte[4];
            if (fp.Read(tag, 0, 4) != 4) break;
            byte[] szBuf = new byte[4];
            if (fp.Read(szBuf, 0, 4) != 4) break;
            int sz = BitConverter.ToInt32(szBuf, 0);
            if (Encoding.ASCII.GetString(tag) == "data")
            {
                info.DataOffset = (int)fp.Position;
                info.DataSize = sz;
                break;
            }
            fp.Seek(sz, SeekOrigin.Current);
        }
        return info;
    }

    public static short[] ReadSamples(string path, WavInfo info, int maxSamples)
    {
        using var fp = new FileStream(path, FileMode.Open, FileAccess.Read);
        fp.Seek(info.DataOffset, SeekOrigin.Begin);
        byte[] buf = new byte[maxSamples * sizeof(short)];
        int bytesRead = fp.Read(buf, 0, buf.Length);
        int count = bytesRead / sizeof(short);
        short[] samples = new short[count];
        Buffer.BlockCopy(buf, 0, samples, 0, count * sizeof(short));
        return samples;
    }

    public static void Write(string path, short[] samples, int numSamples, int channels, int sampleRate, int bits)
    {
        using var fp = new FileStream(path, FileMode.Create, FileAccess.Write);
        int byteRate = sampleRate * channels * bits / 8;
        int blockAlign = channels * bits / 8;
        int dataBytes = numSamples * channels * (bits / 8);

        WriteAscii(fp, "RIFF");
        WriteInt(fp, 36 + dataBytes);
        WriteAscii(fp, "WAVE");

        WriteAscii(fp, "fmt ");
        WriteInt(fp, 16);
        WriteShort(fp, 1);
        WriteShort(fp, (short)channels);
        WriteInt(fp, sampleRate);
        WriteInt(fp, byteRate);
        WriteShort(fp, (short)blockAlign);
        WriteShort(fp, (short)bits);

        WriteAscii(fp, "data");
        WriteInt(fp, dataBytes);

        byte[] buf = new byte[dataBytes];
        Buffer.BlockCopy(samples, 0, buf, 0, Math.Min(samples.Length * sizeof(short), dataBytes));
        fp.Write(buf, 0, buf.Length);
    }

    private static void WriteAscii(FileStream fp, string s)
    {
        byte[] buf = Encoding.ASCII.GetBytes(s);
        fp.Write(buf, 0, buf.Length);
    }

    private static void WriteInt(FileStream fp, int v)
    {
        byte[] buf = BitConverter.GetBytes(v);
        fp.Write(buf, 0, 4);
    }

    private static void WriteShort(FileStream fp, short v)
    {
        byte[] buf = BitConverter.GetBytes(v);
        fp.Write(buf, 0, 2);
    }
}
