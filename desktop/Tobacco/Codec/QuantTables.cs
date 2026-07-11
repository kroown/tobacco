namespace Tobacco.Codec;

public static class QuantTables
{
    private static readonly int[] DefaultLumaQmatrix = { 16, 11, 10, 16, 12, 12, 14, 19, 14, 14, 18, 23, 18, 22, 23, 24 };
    private static readonly int[] DefaultChromaQmatrix = { 17, 18, 24, 47, 26, 29, 40, 51, 29, 35, 43, 54, 37, 46, 56, 61 };

    public static short[,] LumaQuant { get; } = new short[BluntConstants.MaxQp + 1, BluntConstants.BlockCoeffs];
    public static short[,] ChromaQuant { get; } = new short[BluntConstants.MaxQp + 1, BluntConstants.BlockCoeffs];

    public static void Init(int quality)
    {
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;

        for (int qp = 0; qp <= BluntConstants.MaxQp; qp++)
        {
            for (int i = 0; i < 16; i++)
            {
                int lq = (DefaultLumaQmatrix[i] * scale + 50) / 100;
                int cq = (DefaultChromaQmatrix[i] * scale + 50) / 100;
                if (lq < 1) lq = 1;
                if (lq > 255) lq = 255;
                if (cq < 1) cq = 1;
                if (cq > 255) cq = 255;

                int qpAdj = (qp >> 1) + (qp & 1);
                lq = Clamp16((lq * qpAdj + 2) >> 2);
                cq = Clamp16((cq * qpAdj + 2) >> 2);
                if (lq < 1) lq = 1;
                if (cq < 1) cq = 1;

                LumaQuant[qp, Zigzag.Scan[i]] = (short)lq;
                ChromaQuant[qp, Zigzag.Scan[i]] = (short)cq;
            }
        }
    }

    private static int Clamp16(int v)
    {
        if (v < -32768) return -32768;
        if (v > 32767) return 32767;
        return v;
    }
}
