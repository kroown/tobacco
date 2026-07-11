using System;
using System.IO;

namespace Tobacco.Codec;

public class BluntEncoder : IDisposable
{
    private FileStream _fp;
    private BluntHeader _header;
    private uint _frameCount;
    private byte _quality;
    private int _keyframeInterval;
    private BluntFrame _refFrame;
    private bool _hasReference;
    private bool _audioEnabled;
    private int _audioChannels;
    private int _audioSampleRate;
    private long _lastFrameHdrPos;

    public BluntEncoder()
    {
        _quality = 75;
        _keyframeInterval = 30;
    }

    public void SetQuality(byte quality)
    {
        _quality = quality;
    }

    public void SetAudio(int channels, int sampleRate)
    {
        _audioEnabled = true;
        _audioChannels = channels;
        _audioSampleRate = sampleRate;
    }

    public void Open(string path, ushort width, ushort height, ushort fpsNum, ushort fpsDen)
    {
        _fp = new FileStream(path, FileMode.Create, FileAccess.Write);
        int mbw = (width + BluntConstants.MbSize - 1) / BluntConstants.MbSize;
        int mbh = (height + BluntConstants.MbSize - 1) / BluntConstants.MbSize;

        _header = new BluntHeader();
        _header.Magic = 0x544E4C42;
        _header.Version = BluntConstants.Version;
        _header.HeaderSize = (ushort)BluntConstants.HeaderSize;
        _header.Width = width;
        _header.Height = height;
        _header.FpsNum = fpsNum;
        _header.FpsDen = fpsDen;
        _header.NumFrames = 0;
        _header.MbWidth = (ushort)mbw;
        _header.MbHeight = (ushort)mbh;
        _header.Flags = 0;
        if (_audioEnabled)
            _header.Flags |= (byte)BluntConstants.FlagHasAudio;
        _header.Quality = _quality;
        _header.NumKeyframes = 0;
        _header.Reserved = new byte[24];

        if (_audioEnabled)
        {
            _header.AudioSampleRate = (ushort)_audioSampleRate;
            _header.AudioChannels = (byte)_audioChannels;
            _header.AudioBits = 16;
        }

        byte[] hdrBuf = StructToBytes(_header);
        _fp.Write(hdrBuf, 0, hdrBuf.Length);
        _fp.Flush();

        _frameCount = 0;
        _hasReference = false;

        QuantTables.Init(_quality);
        AudioQuantTables.Init(_quality);
        _refFrame = new BluntFrame();
        _refFrame.Alloc(mbw, mbh);
    }

    public void WriteFrame(BluntFrame frame, bool forceKeyframe)
    {
        int w = _header.Width;
        int h = _header.Height;
        int mbw = _header.MbWidth;
        int mbh = _header.MbHeight;
        int qp = ((100 - _quality) * BluntConstants.MaxQp) / 100;
        if (qp < 1) qp = 1;

        bool isKeyframe = forceKeyframe || !_hasReference || (_frameCount % _keyframeInterval == 0);

        var bw = new BitWriter(w * h * 4);
        if (isKeyframe)
            EncodeFrameI(bw, frame, mbw, mbh, qp);
        else
            EncodeFrameP(bw, frame, _refFrame, mbw, mbh, w, h, qp);

        int dataSize = bw.Flush();

        var fhdr = new BluntFrameHeader();
        fhdr.FrameNum = _frameCount;
        fhdr.FrameType = (byte)(isKeyframe ? BluntConstants.FrameI : BluntConstants.FrameP);
        fhdr.DataSize = (uint)dataSize;
        fhdr.TimestampMs = (uint)((ulong)_frameCount * _header.FpsDen * 1000 / _header.FpsNum);
        fhdr.RefFrame = (ushort)(isKeyframe ? 0 : _frameCount - 1);
        fhdr.AudioDataSize = 0;

        _lastFrameHdrPos = _fp.Position;
        byte[] fhdrBuf = StructToBytes(fhdr);
        _fp.Write(fhdrBuf, 0, fhdrBuf.Length);
        _fp.Write(bw.GetBuffer(), 0, dataSize);
        _fp.Flush();

        Array.Copy(frame.Y, _refFrame.Y, frame.Y.Length);
        Array.Copy(frame.Cb, _refFrame.Cb, frame.Cb.Length);
        Array.Copy(frame.Cr, _refFrame.Cr, frame.Cr.Length);
        _hasReference = true;

        _frameCount++;
    }

    public void WriteAudioFrame(short[] samples, int numSamples)
    {
        if (!_audioEnabled || _fp == null) return;

        int qp = ((100 - _quality) * 63) / 100;
        if (qp < 0) qp = 0;

        byte[] buf = new byte[65536];
        int sz = AudioCodec.EncodeFrame(samples, numSamples, _audioChannels, qp, buf);
        if (sz < 0) return;

        long cur = _fp.Position;
        _fp.Seek(_lastFrameHdrPos + 15, SeekOrigin.Begin);
        byte[] aszBuf = BitConverter.GetBytes((ushort)sz);
        _fp.Write(aszBuf, 0, 2);
        _fp.Seek(cur, SeekOrigin.Begin);

        _fp.Write(buf, 0, sz);
        _fp.Flush();
    }

    public void Close()
    {
        if (_fp == null) return;
        _header.NumFrames = _frameCount;
        _fp.Seek(0, SeekOrigin.Begin);
        byte[] hdrBuf = StructToBytes(_header);
        _fp.Write(hdrBuf, 0, hdrBuf.Length);
        _fp.Dispose();
        _fp = null;
    }

    public void Dispose()
    {
        Close();
    }

    private void EncodeFrameI(BitWriter bw, BluntFrame frame, int mbw, int mbh, int qp)
    {
        short[] block = new short[16];
        short[] coeff = new short[16];
        short[] zigzag = new short[16];

        for (int mbY = 0; mbY < mbh; mbY++)
        {
            for (int mbX = 0; mbX < mbw; mbX++)
            {
                for (int by = 0; by < 4; by++)
                {
                    for (int bx = 0; bx < 4; bx++)
                    {
                        int px = mbX * BluntConstants.MbSize + bx * BluntConstants.BlockSize;
                        int py = mbY * BluntConstants.MbSize + by * BluntConstants.BlockSize;
                        CopyBlockFromPlane(frame.Y, frame.YStride, block, px, py);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.LumaQuant, qp);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        CopyBlockFromPlane(frame.Cb, frame.CbStride, block, cpx, cpy);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.ChromaQuant, qp);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        CopyBlockFromPlane(frame.Cr, frame.CrStride, block, cpx, cpy);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.ChromaQuant, qp);
                    }
                }
            }
        }
    }

    private void EncodeFrameP(BitWriter bw, BluntFrame cur, BluntFrame refFrame, int mbw, int mbh, int w, int h, int qp)
    {
        short[] block = new short[16];
        short[] coeff = new short[16];
        short[] zigzag = new short[16];

        for (int mbY = 0; mbY < mbh; mbY++)
        {
            for (int mbX = 0; mbX < mbw; mbX++)
            {
                for (int by = 0; by < 4; by++)
                {
                    for (int bx = 0; bx < 4; bx++)
                    {
                        int px = mbX * BluntConstants.MbSize + bx * BluntConstants.BlockSize;
                        int py = mbY * BluntConstants.MbSize + by * BluntConstants.BlockSize;
                        MotionEstimate(cur.Y, cur.YStride, refFrame.Y, refFrame.YStride, px, py, w - px, h - py, out int mvx, out int mvy);
                        uint mvEnc = (uint)(((uint)(mvy & 0xFF) << 8) | (uint)(mvx & 0xFF));
                        bw.Write(mvEnc, 16);
                        ComputeResidual(cur.Y, cur.YStride, refFrame.Y, refFrame.YStride, block, px, py, mvx, mvy, w, h);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.LumaQuant, qp);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        MotionEstimate(cur.Cb, cur.CbStride, refFrame.Cb, refFrame.CbStride, cpx, cpy, w / 2 - cpx, h / 2 - cpy, out int cmvx, out int cmvy);
                        uint cmvEnc = (uint)(((uint)(cmvy & 0xFF) << 8) | (uint)(cmvx & 0xFF));
                        bw.Write(cmvEnc, 16);
                        ComputeResidual(cur.Cb, cur.CbStride, refFrame.Cb, refFrame.CbStride, block, cpx, cpy, cmvx, cmvy, w / 2, h / 2);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.ChromaQuant, qp);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        uint cmvEnc = 0;
                        bw.Write(cmvEnc, 16);
                        ComputeResidual(cur.Cr, cur.CrStride, refFrame.Cr, refFrame.CrStride, block, cpx, cpy, 0, 0, w / 2, h / 2);
                        WhtTransform.ForwardDct4x4(block, coeff);
                        QuantizeAndEncode(bw, coeff, QuantTables.ChromaQuant, qp);
                    }
                }
            }
        }
    }

    private static void MotionEstimate(byte[] cur, int cs, byte[] refPlane, int rs, int px, int py, int rw, int rh, out int outDx, out int outDy)
    {
        int bestSad = int.MaxValue;
        int bestDx = 0, bestDy = 0;
        int searchRange = 16;

        for (int dy = -searchRange; dy <= searchRange; dy += 2)
        {
            for (int dx = -searchRange; dx <= searchRange; dx += 2)
            {
                int sx = dx < 0 ? 0 : dx;
                int sy = dy < 0 ? 0 : dy;
                if (sx + 16 > rw) sx = rw - 16;
                if (sy + 16 > rh) sy = rh - 16;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;

                int sad = ComputeSad(cur, cs, refPlane, rs, px, py, sx, sy, rw, rh);
                if (sad < bestSad)
                {
                    bestSad = sad;
                    bestDx = dx;
                    bestDy = dy;
                }
            }
        }

        outDx = bestDx;
        outDy = bestDy;
    }

    private static int ComputeSad(byte[] a, int asStride, byte[] b, int bsStride, int px, int py, int sx, int sy, int w, int h)
    {
        int sad = 0;
        for (int y = 0; y < 16 && py + y < h; y++)
        {
            for (int x = 0; x < 16 && px + x < w; x++)
            {
                sad += Math.Abs((int)a[(py + y) * asStride + (px + x)] - (int)b[(sy + y) * bsStride + (sx + x)]);
            }
        }
        return sad;
    }

    private static void ComputeResidual(byte[] cur, int cs, byte[] refPlane, int rs, short[] block, int px, int py, int dx, int dy, int w, int h)
    {
        int bx = dx >> 1;
        int by = dy >> 1;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                int cy = py + y;
                int cx = px + x;
                if (cy >= h || cx >= w) { block[y * 4 + x] = 0; continue; }
                int sy = Math.Clamp(cy + by, 0, h - 1);
                int sx = Math.Clamp(cx + bx, 0, w - 1);
                block[y * 4 + x] = (short)(cur[cy * cs + cx] - refPlane[sy * rs + sx]);
            }
        }
    }

    private static void QuantizeAndEncode(BitWriter bw, short[] coeff, short[,] qtable, int qp)
    {
        short[] zigzag = new short[16];
        for (int i = 0; i < 16; i++)
        {
            int q = qtable[qp, i];
            if (q == 0) q = 1;
            short val = (short)((coeff[i] + (q >> 1)) / q);
            zigzag[Zigzag.Scan[i]] = val;
        }
        EncodeBlockRunlevel(bw, zigzag);
    }

    private static void EncodeBlockRunlevel(BitWriter bw, short[] zigzagBlock)
    {
        int idx = 0, run = 0;
        while (idx < 16)
        {
            if (zigzagBlock[idx] == 0) { run++; idx++; continue; }
            int level = zigzagBlock[idx];
            int absLevel = level < 0 ? -level : level;
            int sign = level < 0 ? 1 : 0;
            if (absLevel <= 14)
            {
                bw.Write((uint)((run << 4) | absLevel), 8);
                bw.Write((uint)sign, 1);
            }
            else
            {
                bw.Write((uint)((run << 4) | 0x0F), 8);
                bw.Write((uint)(ushort)(short)level, 16);
            }
            run = 0;
            idx++;
        }
        bw.Write(0x00, 8);
    }

    private static void CopyBlockFromPlane(byte[] plane, int stride, short[] block, int bx, int by)
    {
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                block[y * 4 + x] = plane[(by + y) * stride + (bx + x)];
    }

    private static byte[] StructToBytes<T>(T s) where T : struct
    {
        int size = System.Runtime.InteropServices.Marshal.SizeOf<T>();
        byte[] buf = new byte[size];
        var handle = System.Runtime.InteropServices.GCHandle.Alloc(buf, System.Runtime.InteropServices.GCHandleType.Pinned);
        try
        {
            System.Runtime.InteropServices.Marshal.StructureToPtr(s, handle.AddrOfPinnedObject(), false);
        }
        finally
        {
            handle.Free();
        }
        return buf;
    }

    private static T BytesToStruct<T>(byte[] buf) where T : struct
    {
        var handle = System.Runtime.InteropServices.GCHandle.Alloc(buf, System.Runtime.InteropServices.GCHandleType.Pinned);
        try
        {
            return System.Runtime.InteropServices.Marshal.PtrToStructure<T>(handle.AddrOfPinnedObject());
        }
        finally
        {
            handle.Free();
        }
    }
}
