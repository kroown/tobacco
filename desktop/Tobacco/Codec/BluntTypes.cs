using System;
using System.Runtime.InteropServices;

namespace Tobacco.Codec;

public static class BluntConstants
{
    public const string Magic = "BLNT";
    public const uint MagicUInt32 = 0x544E4C42;
    public const int Version = 1;
    public const int HeaderSize = 64;
    public const int FrameHeaderSize = 17;
    public const int MaxWidth = 16384;
    public const int MaxHeight = 16384;
    public const int MbSize = 16;
    public const int BlockSize = 4;
    public const int BlockArea = 16;
    public const int FlagAlpha = 1 << 0;
    public const int FlagInterlace = 1 << 1;
    public const int FlagHasAudio = 1 << 2;
    public const int FrameI = 0;
    public const int FrameP = 1;
    public const int MaxFrames = 1000000;
    public const int MaxQp = 51;
    public const int BlockCoeffs = 16;
    public const int AudioBlockSamples = 16;
    public const int AudioMaxChannels = 2;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct BluntHeader
{
    public uint Magic;
    public ushort Version;
    public ushort HeaderSize;
    public ushort Width;
    public ushort Height;
    public ushort FpsNum;
    public ushort FpsDen;
    public uint NumFrames;
    public ushort MbWidth;
    public ushort MbHeight;
    public byte Flags;
    public byte Quality;
    public ushort NumKeyframes;
    public ushort AudioSampleRate;
    public byte AudioChannels;
    public byte AudioBits;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 24)]
    public byte[] Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct BluntFrameHeader
{
    public uint FrameNum;
    public byte FrameType;
    public uint DataSize;
    public uint TimestampMs;
    public ushort RefFrame;
    public ushort AudioDataSize;
}

public class BluntFrame : IDisposable
{
    public byte[] Y { get; set; } = Array.Empty<byte>();
    public byte[] Cb { get; set; } = Array.Empty<byte>();
    public byte[] Cr { get; set; } = Array.Empty<byte>();
    public int YStride { get; set; }
    public int CbStride { get; set; }
    public int CrStride { get; set; }

    public void Alloc(int mbWidth, int mbHeight)
    {
        int w = mbWidth * BluntConstants.MbSize;
        int h = mbHeight * BluntConstants.MbSize;
        int cw = (w + 1) / 2;
        int ch = (h + 1) / 2;
        YStride = w;
        CbStride = cw;
        CrStride = cw;
        Y = new byte[YStride * h];
        Cb = new byte[CbStride * ch];
        Cr = new byte[CrStride * ch];
    }

    public void Dispose() { }
}
