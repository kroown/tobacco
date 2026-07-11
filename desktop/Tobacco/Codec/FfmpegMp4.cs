using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Tobacco.Codec;

public static class FfmpegMp4
{
    public static bool IsAvailable()
    {
        try
        {
            var psi = new ProcessStartInfo("ffmpeg", "-version")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using var p = Process.Start(psi);
            p?.WaitForExit(3000);
            return p?.ExitCode == 0;
        }
        catch { return false; }
    }

    public static void Mp4ToBlunt(string mp4Path, string bluntPath, int quality, Action<string>? log = null,
        CancellationToken ct = default, Action<double>? progress = null)
    {
        log?.Invoke("Probing video info...");
        var info = ProbeVideo(mp4Path);
        log?.Invoke($"  {info.Width}x{info.Height} {info.Fps:F1}fps {info.Duration:F1}s");

        int fpsNum = (int)Math.Round(info.Fps);
        if (fpsNum < 1) fpsNum = 30;
        if (fpsNum > 120) fpsNum = 120;

        bool hasAudio = info.HasAudio;
        if (hasAudio)
            log?.Invoke($"  Audio: {info.AudioSampleRate}Hz {info.AudioChannels}ch");

        var enc = new BluntEncoder();
        enc.SetQuality((byte)quality);
        if (hasAudio)
            enc.SetAudio(info.AudioChannels, info.AudioSampleRate);
        enc.Open(bluntPath, (ushort)info.Width, (ushort)info.Height, (ushort)fpsNum, 1);

        int mbw = (info.Width + 15) / 16;
        int mbh = (info.Height + 15) / 16;
        int frameSize = info.Width * info.Height * 3 / 2;

        var videoProcess = StartFfmpeg(
            $"-i \"{mp4Path}\" -f rawvideo -pix_fmt yuv420p -vf \"scale={info.Width}:{info.Height}\" -",
            redirectStd: true);

        Process? audioProcess = null;
        if (hasAudio)
        {
            audioProcess = StartFfmpeg(
                $"-i \"{mp4Path}\" -f s16le -acodec pcm_s16le -ar {info.AudioSampleRate} -ac {info.AudioChannels} -",
                redirectStd: true);
        }

        log?.Invoke("Encoding frames...");
        int frameCount = 0;
        int totalFrames = (int)(info.Duration * fpsNum);

        while (!ct.IsCancellationRequested)
        {
            byte[] yuvFrame = ReadExact(videoProcess.StandardOutput.BaseStream, frameSize);
            if (yuvFrame == null) break;

            var frame = new BluntFrame();
            frame.Alloc(mbw, mbh);
            Buffer.BlockCopy(yuvFrame, 0, frame.Y, 0, frame.Y.Length);
            int ySize = frame.Y.Length;
            int uvSize = frame.Cb.Length;
            Buffer.BlockCopy(yuvFrame, ySize, frame.Cb, 0, uvSize);
            Buffer.BlockCopy(yuvFrame, ySize + uvSize, frame.Cr, 0, uvSize);

            bool isKey = frameCount == 0 || frameCount % (fpsNum * 3) == 0;
            enc.WriteFrame(frame, isKey);

            if (hasAudio && audioProcess != null)
            {
                int samplesPerFrame = info.AudioSampleRate / fpsNum;
                int bytesPerFrame = samplesPerFrame * info.AudioChannels * 2;
                byte[] audioRaw = ReadExact(audioProcess.StandardOutput.BaseStream, bytesPerFrame);
                if (audioRaw != null)
                {
                    short[] audioSamples = new short[audioRaw.Length / 2];
                    Buffer.BlockCopy(audioRaw, 0, audioSamples, 0, audioRaw.Length);
                    enc.WriteAudioFrame(audioSamples, audioSamples.Length);
                }
            }

            frameCount++;
            if (frameCount % 30 == 0)
                log?.Invoke($"  Frame {frameCount}/{totalFrames}");
            progress?.Invoke(totalFrames > 0 ? (double)frameCount / totalFrames : 0);
        }

        enc.Close();
        try { videoProcess.StandardOutput.BaseStream.Close(); videoProcess.Kill(); } catch { }
        try { audioProcess?.StandardOutput.BaseStream.Close(); audioProcess?.Kill(); } catch { }

        log?.Invoke($"Done! {frameCount} frames written to {Path.GetFileName(bluntPath)}");
        progress?.Invoke(1.0);
    }

    public static void BluntToMp4(string bluntPath, string mp4Path, int quality, Action<string>? log = null,
        CancellationToken ct = default, Action<double>? progress = null)
    {
        log?.Invoke("Reading BLUNT file...");
        using var dec = new BluntDecoder();
        dec.Open(bluntPath);
        dec.ReadHeader();
        var hdr = dec.Header;

        double fps = hdr.FpsNum > 0 ? (double)hdr.FpsNum / hdr.FpsDen : 30.0;
        log?.Invoke($"  {hdr.Width}x{hdr.Height} {hdr.NumFrames} frames {fps:F1}fps");

        bool hasAudio = (hdr.Flags & BluntConstants.FlagHasAudio) != 0;
        if (hasAudio)
            log?.Invoke($"  Audio: {hdr.AudioSampleRate}Hz {hdr.AudioChannels}ch");

        var videoProcess = StartFfmpeg(
            $"-f rawvideo -pix_fmt yuv420p -s {hdr.Width}x{hdr.Height} -r {fps:F2} -i - " +
            $"-c:v libx264 -crf {Math.Max(18, 51 - quality)} -preset medium -pix_fmt yuv420p " +
            $"-movflags +faststart \"{mp4Path}\"",
            redirectStd: true, stdin: true);

        Process? audioProcess = null;
        string? tempAudioPath = null;
        if (hasAudio)
        {
            tempAudioPath = Path.Combine(Path.GetTempPath(), "tobacco_audio_" + Guid.NewGuid().ToString("N") + ".wav");
            audioProcess = StartFfmpeg(
                $"-f s16le -ar {hdr.AudioSampleRate} -ac {hdr.AudioChannels} -i - " +
                $"-c:a aac -b:a 128k -y \"{tempAudioPath}\"",
                redirectStd: true, stdin: true);
        }

        log?.Invoke("Decoding and encoding frames...");
        for (uint f = 0; f < hdr.NumFrames && !ct.IsCancellationRequested; f++)
        {
            var frame = dec.ReadFrame();
            if (frame == null) break;

            byte[] yuv = new byte[hdr.Width * hdr.Height * 3 / 2];
            int ySize = hdr.Width * hdr.Height;
            int uvSize = (hdr.Width / 2) * (hdr.Height / 2);
            Buffer.BlockCopy(frame.Y, 0, yuv, 0, Math.Min(frame.Y.Length, ySize));
            Buffer.BlockCopy(frame.Cb, 0, yuv, ySize, Math.Min(frame.Cb.Length, uvSize));
            Buffer.BlockCopy(frame.Cr, 0, yuv, ySize + uvSize, Math.Min(frame.Cr.Length, uvSize));

            videoProcess.StandardInput.BaseStream.Write(yuv, 0, yuv.Length);
            videoProcess.StandardInput.BaseStream.Flush();

            if (hasAudio && audioProcess != null)
            {
                int samplesPerFrame = hdr.AudioSampleRate / (int)fps;
                short[] audio = dec.ReadAudioFrame(samplesPerFrame * hdr.AudioChannels);
                if (audio.Length > 0)
                {
                    byte[] audioBytes = new byte[audio.Length * 2];
                    Buffer.BlockCopy(audio, 0, audioBytes, 0, audioBytes.Length);
                    audioProcess.StandardInput.BaseStream.Write(audioBytes, 0, audioBytes.Length);
                    audioProcess.StandardInput.BaseStream.Flush();
                }
            }

            if (f % 30 == 0)
                log?.Invoke($"  Frame {f + 1}/{hdr.NumFrames}");
            progress?.Invoke((double)(f + 1) / hdr.NumFrames);
        }

        try { videoProcess.StandardInput.Close(); videoProcess.WaitForExit(10000); } catch { }
        try { audioProcess?.StandardInput.Close(); audioProcess?.WaitForExit(10000); } catch { }

        if (hasAudio && tempAudioPath != null && File.Exists(tempAudioPath))
        {
            log?.Invoke("Muxing audio...");
            var muxProcess = StartFfmpeg(
                $"-i \"{mp4Path}\" -i \"{tempAudioPath}\" -c:v copy -c:a aac -shortest -y \"{mp4Path}.mux.mp4\"",
                redirectStd: false);
            muxProcess.WaitForExit(30000);
            if (muxProcess.ExitCode == 0)
            {
                File.Delete(mp4Path);
                File.Move(mp4Path + ".mux.mp4", mp4Path);
            }
            try { File.Delete(tempAudioPath); } catch { }
        }

        log?.Invoke($"Done! Written to {Path.GetFileName(mp4Path)}");
        progress?.Invoke(1.0);
    }

    private struct ProbeResult
    {
        public int Width, Height;
        public double Fps, Duration;
        public bool HasAudio;
        public int AudioSampleRate, AudioChannels;
    }

    private static ProbeResult ProbeVideo(string path)
    {
        var psi = new ProcessStartInfo("ffprobe",
            $"-v quiet -print_format json -show_format -show_streams \"{path}\"")
        {
            RedirectStandardOutput = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        using var p = Process.Start(psi)!;
        string json = p.StandardOutput.ReadToEnd();
        p.WaitForExit(5000);

        var result = new ProbeResult { Fps = 30, AudioSampleRate = 44100, AudioChannels = 2 };

        int wIdx = json.IndexOf("\"width\":");
        if (wIdx >= 0) result.Width = ParseInt(json, wIdx + 8);
        int hIdx = json.IndexOf("\"height\":");
        if (hIdx >= 0) result.Height = ParseInt(json, hIdx + 9);

        int fpsIdx = json.IndexOf("\"r_frame_rate\":");
        if (fpsIdx >= 0)
        {
            string rateStr = ExtractQuoted(json, fpsIdx + 15);
            if (rateStr.Contains('/'))
            {
                var parts = rateStr.Split('/');
                if (double.TryParse(parts[0], out double num) && double.TryParse(parts[1], out double den) && den > 0)
                    result.Fps = num / den;
            }
            else double.TryParse(rateStr, out result.Fps);
        }

        int durIdx = json.IndexOf("\"duration\":");
        if (durIdx >= 0) result.Duration = ParseDouble(json, durIdx + 11);

        result.HasAudio = json.Contains("\"codec_type\": \"audio\"");

        int srIdx = json.IndexOf("\"sample_rate\":");
        if (srIdx >= 0) result.AudioSampleRate = ParseInt(json, srIdx + 14);
        int chIdx = json.IndexOf("\"channels\":");
        if (chIdx >= 0) result.AudioChannels = ParseInt(json, chIdx + 11);

        if (result.Width <= 0) result.Width = 320;
        if (result.Height <= 0) result.Height = 240;
        if (result.Duration <= 0) result.Duration = 10;
        return result;
    }

    private static int ParseInt(string s, int offset)
    {
        while (offset < s.Length && !char.IsDigit(s[offset]) && s[offset] != '-') offset++;
        string num = "";
        while (offset < s.Length && (char.IsDigit(s[offset]) || s[offset] == '-'))
            num += s[offset++];
        return int.TryParse(num, out int v) ? v : 0;
    }

    private static double ParseDouble(string s, int offset)
    {
        while (offset < s.Length && !char.IsDigit(s[offset]) && s[offset] != '-') offset++;
        string num = "";
        while (offset < s.Length && (char.IsDigit(s[offset]) || s[offset] == '-' || s[offset] == '.'))
            num += s[offset++];
        return double.TryParse(num, out double v) ? v : 0;
    }

    private static string ExtractQuoted(string s, int offset)
    {
        while (offset < s.Length && s[offset] != '"') offset++;
        offset++;
        string result = "";
        while (offset < s.Length && s[offset] != '"')
            result += s[offset++];
        return result;
    }

    private static Process StartFfmpeg(string args, bool redirectStd = false, bool stdin = false)
    {
        var psi = new ProcessStartInfo("ffmpeg", args + " -y")
        {
            RedirectStandardInput = stdin,
            RedirectStandardOutput = redirectStd && !stdin,
            RedirectStandardError = !redirectStd,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        return Process.Start(psi)!;
    }

    private static byte[]? ReadExact(Stream stream, int count)
    {
        byte[] buf = new byte[count];
        int offset = 0;
        while (offset < count)
        {
            int read;
            try { read = stream.Read(buf, offset, count - offset); }
            catch { return null; }
            if (read <= 0) return null;
            offset += read;
        }
        return buf;
    }
}
