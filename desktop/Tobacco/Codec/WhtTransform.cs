namespace Tobacco.Codec;

public static class WhtTransform
{
    public static void ForwardDct4x4(short[] input, short[] output)
    {
        short[] tmp = new short[16];
        for (int i = 0; i < 4; i++)
        {
            int s0 = input[i * 4 + 0], s1 = input[i * 4 + 1];
            int s2 = input[i * 4 + 2], s3 = input[i * 4 + 3];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            tmp[i * 4 + 0] = (short)(a + c);
            tmp[i * 4 + 1] = (short)(a - c);
            tmp[i * 4 + 2] = (short)(b - d);
            tmp[i * 4 + 3] = (short)(b + d);
        }
        for (int i = 0; i < 4; i++)
        {
            int s0 = tmp[0 * 4 + i], s1 = tmp[1 * 4 + i];
            int s2 = tmp[2 * 4 + i], s3 = tmp[3 * 4 + i];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            output[0 * 4 + i] = (short)(a + c);
            output[1 * 4 + i] = (short)(a - c);
            output[2 * 4 + i] = (short)(b - d);
            output[3 * 4 + i] = (short)(b + d);
        }
    }

    public static void InverseDct4x4(short[] block)
    {
        for (int i = 0; i < 4; i++)
        {
            int s0 = block[0 * 4 + i], s1 = block[1 * 4 + i];
            int s2 = block[2 * 4 + i], s3 = block[3 * 4 + i];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            block[0 * 4 + i] = (short)((a + c + 2) >> 2);
            block[1 * 4 + i] = (short)((a - c + 2) >> 2);
            block[2 * 4 + i] = (short)((b - d + 2) >> 2);
            block[3 * 4 + i] = (short)((b + d + 2) >> 2);
        }
        for (int i = 0; i < 4; i++)
        {
            int s0 = block[i * 4 + 0], s1 = block[i * 4 + 1];
            int s2 = block[i * 4 + 2], s3 = block[i * 4 + 3];
            int a = s0 + s1, c = s2 + s3;
            int b = s0 - s1, d = s2 - s3;
            block[i * 4 + 0] = (short)((a + c + 2) >> 2);
            block[i * 4 + 1] = (short)((a - c + 2) >> 2);
            block[i * 4 + 2] = (short)((b - d + 2) >> 2);
            block[i * 4 + 3] = (short)((b + d + 2) >> 2);
        }
    }

    public static void DequantizeBlock(short[] block, int qp)
    {
        if (qp < 0) qp = 0;
        if (qp > BluntConstants.MaxQp) qp = BluntConstants.MaxQp;
        for (int i = 0; i < 16; i++)
            block[i] *= QuantTables.LumaQuant[qp, i];
    }
}
