using System;

namespace Tobacco.Codec;

public static class AudioCodec
{
    public static void EncodeBlock(short[] input, int[] output, int qp)
    {
        if (qp < 0) qp = 0;
        if (qp > 63) qp = 63;

        int[] coeff = new int[16];
        int[] tmp = new int[16];
        for (int i = 0; i < 4; i++)
        {
            int s0 = input[i * 4 + 0], s1 = input[i * 4 + 1];
            int s2 = input[i * 4 + 2], s3 = input[i * 4 + 3];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            tmp[i * 4 + 0] = a + c;
            tmp[i * 4 + 1] = a - c;
            tmp[i * 4 + 2] = b - d;
            tmp[i * 4 + 3] = b + d;
        }
        for (int i = 0; i < 4; i++)
        {
            int s0 = tmp[0 * 4 + i], s1 = tmp[1 * 4 + i];
            int s2 = tmp[2 * 4 + i], s3 = tmp[3 * 4 + i];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            coeff[0 * 4 + i] = a + c;
            coeff[1 * 4 + i] = a - c;
            coeff[2 * 4 + i] = b - d;
            coeff[3 * 4 + i] = b + d;
        }

        for (int i = 0; i < 16; i++)
        {
            int q = AudioQuantTables.Quant[qp, i];
            if (q == 0) q = 1;
            output[i] = (coeff[i] + (q >> 1)) / q;
        }
    }

    public static void DecodeBlock(int[] input, short[] output, int qp)
    {
        if (qp < 0) qp = 0;
        if (qp > 63) qp = 63;

        int[] coeff = new int[16];
        for (int i = 0; i < 16; i++)
        {
            int q = AudioQuantTables.Quant[qp, i];
            coeff[i] = input[i] * q;
        }

        int[] tmp = new int[16];
        for (int i = 0; i < 4; i++)
        {
            int s0 = coeff[0 * 4 + i], s1 = coeff[1 * 4 + i];
            int s2 = coeff[2 * 4 + i], s3 = coeff[3 * 4 + i];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            tmp[0 * 4 + i] = (a + c + 2) >> 2;
            tmp[1 * 4 + i] = (a - c + 2) >> 2;
            tmp[2 * 4 + i] = (b - d + 2) >> 2;
            tmp[3 * 4 + i] = (b + d + 2) >> 2;
        }
        for (int i = 0; i < 4; i++)
        {
            int s0 = tmp[i * 4 + 0], s1 = tmp[i * 4 + 1];
            int s2 = tmp[i * 4 + 2], s3 = tmp[i * 4 + 3];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            output[i * 4 + 0] = Clamp16((a + c + 2) >> 2);
            output[i * 4 + 1] = Clamp16((a - c + 2) >> 2);
            output[i * 4 + 2] = Clamp16((b - d + 2) >> 2);
            output[i * 4 + 3] = Clamp16((b + d + 2) >> 2);
        }
    }

    private static short Clamp16(int v)
    {
        if (v < -32768) return -32768;
        if (v > 32767) return 32767;
        return (short)v;
    }

    private static void EncodeRunLevel(BitWriter bw, int[] zigzag)
    {
        int idx = 0, run = 0;
        while (idx < 16)
        {
            if (zigzag[idx] == 0) { run++; idx++; continue; }
            int level = zigzag[idx];
            int absLevel = level < 0 ? -level : level;
            int sign = level < 0 ? 1 : 0;
            if (absLevel <= 13)
            {
                bw.Write((uint)((run << 4) | absLevel), 8);
                bw.Write((uint)sign, 1);
            }
            else if (absLevel <= 32767)
            {
                bw.Write((uint)((run << 4) | 0x0E), 8);
                bw.Write((uint)(ushort)(short)level, 16);
            }
            else
            {
                bw.Write((uint)((run << 4) | 0x0F), 8);
                bw.Write((uint)level, 32);
            }
            run = 0;
            idx++;
        }
        bw.Write(0x00, 8);
    }

    private static int DecodeRunLevel(BitReader br, int[] block)
    {
        int idx = 0;
        bool hitEob = false;
        while (idx < 16)
        {
            uint rl = br.Read(8);
            int run = (int)((rl >> 4) & 0xF);
            int level = (int)(rl & 0xF);
            if (level == 0 && run == 0) { hitEob = true; break; }
            if (level == 0xF)
            {
                int raw = (int)(int)(uint)br.Read(32);
                idx += run;
                if (idx < 16) block[idx] = raw;
                idx++;
            }
            else if (level == 0xE)
            {
                int raw = (int)(short)(ushort)br.Read(16);
                idx += run;
                if (idx < 16) block[idx] = raw;
                idx++;
            }
            else
            {
                int sign = (int)br.Read(1);
                int val = sign == 1 ? -level : level;
                idx += run;
                if (idx < 16) block[idx] = val;
                idx++;
            }
        }
        if (!hitEob && idx == 16)
            br.Read(8);
        return 0;
    }

    public static int EncodeFrame(short[] samples, int numSamples, int channels, int qp, byte[] outBuf)
    {
        int samplesPerBlock = BluntConstants.AudioBlockSamples;
        int blocks = (numSamples + samplesPerBlock - 1) / samplesPerBlock;

        var bw = new BitWriter(numSamples * channels * 4);
        short[] block = new short[16];

        for (int ch = 0; ch < channels; ch++)
        {
            for (int b = 0; b < blocks; b++)
            {
                Array.Clear(block, 0, 16);
                for (int i = 0; i < samplesPerBlock; i++)
                {
                    int idx = b * samplesPerBlock + i;
                    if (idx < numSamples)
                        block[i] = samples[idx * channels + ch];
                }
                int[] coded = new int[16];
                int[] zigzag = new int[16];
                EncodeBlock(block, coded, qp);
                for (int i = 0; i < 16; i++)
                    zigzag[i] = coded[Zigzag.Scan[i]];
                EncodeRunLevel(bw, zigzag);
            }
        }

        int sz = bw.Flush();
        if (sz > outBuf.Length) return -1;
        Array.Copy(bw.GetBuffer(), outBuf, sz);
        return sz;
    }

    public static int DecodeFrame(byte[] data, int dataSize, int channels, int qp, short[] outBuf, int outCap)
    {
        var br = new BitReader(data, dataSize);
        int chBlocks = (outCap / channels + BluntConstants.AudioBlockSamples - 1) / BluntConstants.AudioBlockSamples;
        int written = 0;

        for (int ch = 0; ch < channels; ch++)
        {
            for (int b = 0; b < chBlocks; b++)
            {
                int[] zigzag = new int[16];
                int[] coded = new int[16];
                short[] decoded = new short[16];
                DecodeRunLevel(br, zigzag);
                for (int i = 0; i < 16; i++)
                    coded[Zigzag.Scan[i]] = zigzag[i];
                DecodeBlock(coded, decoded, qp);
                for (int i = 0; i < BluntConstants.AudioBlockSamples; i++)
                {
                    int sampleIdx = b * BluntConstants.AudioBlockSamples + i;
                    int outIdx = sampleIdx * channels + ch;
                    if (sampleIdx < outCap / channels && outIdx < outCap)
                    {
                        outBuf[outIdx] = decoded[i];
                        if (outIdx + 1 > written)
                            written = outIdx + 1;
                    }
                }
            }
        }
        return written;
    }
}
