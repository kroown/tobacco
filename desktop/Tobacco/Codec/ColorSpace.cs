using System;

namespace Tobacco.Codec;

public static class ColorSpace
{
    public static void RgbToYCbCr(byte[] rgb, int width, int height, int stride, BluntFrame frame)
    {
        int mbw = (width + BluntConstants.MbSize - 1) / BluntConstants.MbSize;
        int mbh = (height + BluntConstants.MbSize - 1) / BluntConstants.MbSize;
        int paddedW = mbw * BluntConstants.MbSize;
        int paddedH = mbh * BluntConstants.MbSize;
        int cw = (paddedW + 1) / 2;
        int ch = (paddedH + 1) / 2;

        if (frame.Y.Length == 0)
            frame.Alloc(mbw, mbh);

        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                int off = row * stride + col * 3;
                int r = rgb[off + 0];
                int g = rgb[off + 1];
                int b = rgb[off + 2];
                frame.Y[row * frame.YStride + col] = (byte)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                if ((col & 1) == 0 && (row & 1) == 0)
                {
                    frame.Cb[row / 2 * frame.CbStride + col / 2] = (byte)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                    frame.Cr[row / 2 * frame.CrStride + col / 2] = (byte)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
                }
            }
        }

        for (int row = height; row < paddedH; row++)
            Array.Fill(frame.Y, (byte)16, row * frame.YStride, paddedW);
        for (int row = (height + 1) / 2; row < ch; row++)
        {
            Array.Fill(frame.Cb, (byte)128, row * frame.CbStride, cw);
            Array.Fill(frame.Cr, (byte)128, row * frame.CrStride, cw);
        }
    }

    public static void YCbCrToRgb(BluntFrame frame, byte[] rgb, int width, int height, int stride)
    {
        for (int row = 0; row < height; row++)
        {
            for (int col = 0; col < width; col++)
            {
                int yy = frame.Y[row * frame.YStride + col];
                int cbv = frame.Cb[row / 2 * frame.CbStride + col / 2] - 128;
                int crv = frame.Cr[row / 2 * frame.CrStride + col / 2] - 128;
                int off = row * stride + col * 3;
                rgb[off + 0] = (byte)((yy + ((crv * 179) >> 8)));
                rgb[off + 1] = (byte)((yy - ((cbv * 44 + crv * 91) >> 8)));
                rgb[off + 2] = (byte)((yy + ((cbv * 227) >> 8)));
            }
        }
    }
}
