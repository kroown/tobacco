using System;
using System.IO;

namespace Tobacco.Codec;

public class BluntDecoder : IDisposable
{
    private FileStream _fp;
    private BluntHeader _header;
    private BluntFrame _refFrame;
    private bool _hasReference;

    public BluntHeader Header => _header;

    public void Open(string path)
    {
        _fp = new FileStream(path, FileMode.Open, FileAccess.Read);
    }

    public void ReadHeader()
    {
        _fp.Seek(0, SeekOrigin.Begin);
        byte[] buf = new byte[BluntConstants.HeaderSize];
        _fp.Read(buf, 0, buf.Length);
        _header = BytesToStruct<BluntHeader>(buf);
        if (_header.Magic != 0x544E4C42)
            throw new InvalidDataException("Not a BLUNT file");
    }

    public BluntFrame ReadFrame()
    {
        if (_fp.Position >= _fp.Length) return null;

        byte[] fhdrBuf = new byte[BluntConstants.FrameHeaderSize];
        if (_fp.Read(fhdrBuf, 0, fhdrBuf.Length) < fhdrBuf.Length) return null;
        var fhdr = BytesToStruct<BluntFrameHeader>(fhdrBuf);

        byte[] data = new byte[fhdr.DataSize];
        if (_fp.Read(data, 0, (int)fhdr.DataSize) < (int)fhdr.DataSize) return null;

        var frame = new BluntFrame();
        frame.Alloc(_header.MbWidth, _header.MbHeight);

        var br = new BitReader(data, data.Length);

        if (fhdr.FrameType == BluntConstants.FrameI)
            DecodeFrameI(br, frame);
        else
            DecodeFrameP(br, frame);

        if (fhdr.AudioDataSize > 0)
        {
            byte[] audioData = new byte[fhdr.AudioDataSize];
            _fp.Read(audioData, 0, audioData.Length);
        }

        if (!_hasReference)
        {
            _refFrame = new BluntFrame();
            _refFrame.Alloc(_header.MbWidth, _header.MbHeight);
        }
        Array.Copy(frame.Y, _refFrame.Y, frame.Y.Length);
        Array.Copy(frame.Cb, _refFrame.Cb, frame.Cb.Length);
        Array.Copy(frame.Cr, _refFrame.Cr, frame.Cr.Length);
        _hasReference = true;

        return frame;
    }

    public short[] ReadAudioFrame(int maxSamples)
    {
        long savedPos = _fp.Position;
        _fp.Seek(0, SeekOrigin.Begin);
        byte[] hdrBuf = new byte[BluntConstants.HeaderSize];
        _fp.Read(hdrBuf, 0, hdrBuf.Length);
        var hdr = BytesToStruct<BluntHeader>(hdrBuf);

        _fp.Seek(savedPos - BluntConstants.FrameHeaderSize, SeekOrigin.Begin);
        byte[] fhdrBuf = new byte[BluntConstants.FrameHeaderSize];
        _fp.Read(fhdrBuf, 0, fhdrBuf.Length);
        var fhdr = BytesToStruct<BluntFrameHeader>(fhdrBuf);

        _fp.Seek(savedPos, SeekOrigin.Begin);

        if (fhdr.AudioDataSize == 0) return Array.Empty<short>();

        byte[] audioData = new byte[fhdr.AudioDataSize];
        _fp.Read(audioData, 0, audioData.Length);

        int qp = ((100 - hdr.Quality) * 63) / 100;
        short[] outBuf = new short[maxSamples];
        AudioCodec.DecodeFrame(audioData, fhdr.AudioDataSize, hdr.AudioChannels, qp, outBuf, maxSamples);
        return outBuf;
    }

    private void DecodeFrameI(BitReader br, BluntFrame frame)
    {
        int mbw = _header.MbWidth;
        int mbh = _header.MbHeight;
        int qp = ((100 - _header.Quality) * BluntConstants.MaxQp) / 100;
        if (qp < 1) qp = 1;

        short[] block = new short[16];

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
                        DecodeMacroblockI(br, block, QuantTables.LumaQuant);
                        CopyBlockToPlane(frame.Y, frame.YStride, block, px, py);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        DecodeMacroblockI(br, block, QuantTables.ChromaQuant);
                        CopyBlockToPlane(frame.Cb, frame.CbStride, block, cpx, cpy);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        DecodeMacroblockI(br, block, QuantTables.ChromaQuant);
                        CopyBlockToPlane(frame.Cr, frame.CrStride, block, cpx, cpy);
                    }
                }
            }
        }
    }

    private void DecodeFrameP(BitReader br, BluntFrame frame)
    {
        int mbw = _header.MbWidth;
        int mbh = _header.MbHeight;
        int w = _header.Width;
        int h = _header.Height;
        int qp = ((100 - _header.Quality) * BluntConstants.MaxQp) / 100;
        if (qp < 1) qp = 1;

        short[] block = new short[16];

        for (int mbY = 0; mbY < mbh; mbY++)
        {
            for (int mbX = 0; mbX < mbw; mbX++)
            {
                for (int by = 0; by < 4; by++)
                {
                    for (int bx = 0; bx < 4; bx++)
                    {
                        uint mvRaw = br.Read(16);
                        int dx = (int)(mvRaw & 0xFF);
                        int dy = (int)((mvRaw >> 8) & 0xFF);
                        if (dx >= 128) dx -= 256;
                        if (dy >= 128) dy -= 256;

                        int px = mbX * BluntConstants.MbSize + bx * BluntConstants.BlockSize;
                        int py = mbY * BluntConstants.MbSize + by * BluntConstants.BlockSize;

                        MotionCompBlock(frame.Y, frame.YStride, _refFrame.Y, _refFrame.YStride, px, py, dx, dy, w, h);
                        DecodeMacroblockI(br, block, QuantTables.LumaQuant);
                        AddResidualToPlane(frame.Y, frame.YStride, block, px, py, w, h);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        uint mvRaw = br.Read(16);
                        int cmvx = (int)(mvRaw & 0xFF);
                        int cmvy = (int)((mvRaw >> 8) & 0xFF);
                        if (cmvx >= 128) cmvx -= 256;
                        if (cmvy >= 128) cmvy -= 256;

                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        MotionCompBlock(frame.Cb, frame.CbStride, _refFrame.Cb, _refFrame.CbStride, cpx, cpy, cmvx, cmvy, w / 2, h / 2);
                        DecodeMacroblockI(br, block, QuantTables.ChromaQuant);
                        AddResidualToPlane(frame.Cb, frame.CbStride, block, cpx, cpy, w / 2, h / 2);
                    }
                }
                for (int cby = 0; cby < 2; cby++)
                {
                    for (int cbx = 0; cbx < 2; cbx++)
                    {
                        int cpx = mbX * 8 + cbx * BluntConstants.BlockSize;
                        int cpy = mbY * 8 + cby * BluntConstants.BlockSize;
                        MotionCompBlock(frame.Cr, frame.CrStride, _refFrame.Cr, _refFrame.CrStride, cpx, cpy, 0, 0, w / 2, h / 2);
                        DecodeMacroblockI(br, block, QuantTables.ChromaQuant);
                        AddResidualToPlane(frame.Cr, frame.CrStride, block, cpx, cpy, w / 2, h / 2);
                    }
                }
            }
        }
    }

    private static void DecodeMacroblockI(BitReader br, short[] block, short[,] qtable)
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
                short raw = (short)(ushort)br.Read(16);
                idx += run;
                if (idx < 16)
                {
                    int lin = Zigzag.Scan[idx];
                    block[lin] = (short)(raw * qtable[0, lin]);
                }
                idx++;
            }
            else
            {
                int sign = (int)br.Read(1);
                int val = sign == 1 ? -level : level;
                idx += run;
                if (idx < 16)
                {
                    int lin = Zigzag.Scan[idx];
                    block[lin] = (short)(val * qtable[0, lin]);
                }
                idx++;
            }
        }
        if (!hitEob && idx == 16)
            br.Read(8);
    }

    private static void MotionCompBlock(byte[] dst, int ds, byte[] refPlane, int rs, int px, int py, int dx, int dy, int w, int h)
    {
        int bx = dx >> 1;
        int by = dy >> 1;
        for (int y = 0; y < 16 && py + y < h; y++)
        {
            int sy = py + y + by;
            if (sy < 0) sy = 0;
            if (sy >= h) sy = h - 1;
            for (int x = 0; x < 16 && px + x < w; x++)
            {
                int sx = px + x + bx;
                if (sx < 0) sx = 0;
                if (sx >= w) sx = w - 1;
                dst[(py + y) * ds + (px + x)] = refPlane[sy * rs + sx];
            }
        }
    }

    private static void CopyBlockToPlane(byte[] plane, int stride, short[] block, int bx, int by)
    {
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                int val = block[y * 4 + x];
                plane[(by + y) * stride + (bx + x)] = (byte)(val < 0 ? 0 : (val > 255 ? 255 : val));
            }
        }
    }

    private static void AddResidualToPlane(byte[] plane, int stride, short[] block, int bx, int by, int w, int h)
    {
        for (int y = 0; y < 4 && by + y < h; y++)
        {
            for (int x = 0; x < 4 && bx + x < w; x++)
            {
                int val = plane[(by + y) * stride + (bx + x)] + block[y * 4 + x];
                plane[(by + y) * stride + (bx + x)] = (byte)(val < 0 ? 0 : (val > 255 ? 255 : val));
            }
        }
    }

    public void Seek(uint frameNum)
    {
        _fp.Seek(BluntConstants.HeaderSize, SeekOrigin.Begin);
        for (uint i = 0; i < frameNum; i++)
        {
            byte[] fhdrBuf = new byte[BluntConstants.FrameHeaderSize];
            if (_fp.Read(fhdrBuf, 0, fhdrBuf.Length) < fhdrBuf.Length) break;
            var fhdr = BytesToStruct<BluntFrameHeader>(fhdrBuf);
            _fp.Seek(fhdr.DataSize + fhdr.AudioDataSize, SeekOrigin.Current);
        }
    }

    public void Dispose()
    {
        _fp?.Dispose();
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
