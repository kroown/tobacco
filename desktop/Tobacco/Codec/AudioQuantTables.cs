namespace Tobacco.Codec;

public static class AudioQuantTables
{
    private static readonly int[] DefaultQmatrix = { 8, 8, 9, 10, 10, 12, 14, 16, 12, 14, 18, 22, 16, 20, 26, 32 };

    public static int[,] Quant { get; } = new int[64, BluntConstants.AudioBlockSamples];

    public static void Init(int quality)
    {
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;

        for (int qp = 0; qp < 64; qp++)
        {
            int qpAdj = (qp >> 1) + (qp & 1);
            for (int i = 0; i < 16; i++)
            {
                int q = (DefaultQmatrix[i] * scale + 50) / 100;
                q = (q * qpAdj + 2) >> 2;
                if (q < 1) q = 1;
                if (q > 4095) q = 4095;
                Quant[qp, Zigzag.Scan[i]] = q;
            }
        }
    }
}
