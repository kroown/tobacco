using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class ConverterView : UserControl
{
    private string? _inputPath;
    private int _selectedMode;
    private CancellationTokenSource? _cts;

    public ConverterView()
    {
        InitializeComponent();
    }

    private void OnQualityChanged(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        QualityText.Text = ((int)e.NewValue).ToString();
    }

    private void OnSelectMode(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button btn) return;
        _selectedMode = int.Parse((string)btn.Tag!);

        BtnWavToBlunt.Classes.Clear(); BtnWavToBlunt.Classes.Add("button");
        BtnBluntToWav.Classes.Clear(); BtnBluntToWav.Classes.Add("button");
        BtnMp4ToBlunt.Classes.Clear(); BtnMp4ToBlunt.Classes.Add("button");
        BtnBluntToMp4.Classes.Clear(); BtnBluntToMp4.Classes.Add("button");

        btn.Classes.Clear();
        btn.Classes.Add("accent");
    }

    private async void OnOpenFile(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        FilePickerFileType[] types = _selectedMode switch
        {
            0 => new[] { new FilePickerFileType("WAV files") { Patterns = new[] { "*.wav" } } },
            1 => new[] { new FilePickerFileType("BLUNT files") { Patterns = new[] { "*.blunt" } } },
            2 => new[] { new FilePickerFileType("MP4 files") { Patterns = new[] { "*.mp4", "*.mkv", "*.avi" } } },
            3 => new[] { new FilePickerFileType("BLUNT files") { Patterns = new[] { "*.blunt" } } },
            _ => new[] { new FilePickerFileType("All files") { Patterns = new[] { "*.*" } } }
        };

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select input file",
            AllowMultiple = false,
            FileTypeFilter = types
        });
        if (files.Count == 0) return;
        _inputPath = files[0].Path.LocalPath;
        InputFileText.Text = Path.GetFileName(_inputPath);
        InputFileText.Foreground = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#c0c0d0"));
    }

    private async void OnConvert(object? sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_inputPath))
        {
            StatusText.Text = "Select a file first.";
            return;
        }

        int quality = (int)QualitySlider.Value;
        _cts = new CancellationTokenSource();

        try
        {
            LogBox.Text = "";
            switch (_selectedMode)
            {
                case 0:
                    await Task.Run(() => ConvertWavToBlunt(_inputPath, quality));
                    break;
                case 1:
                    await Task.Run(() => ConvertBluntToWav(_inputPath, quality));
                    break;
                case 2:
                    await Task.Run(() => ConvertMp4ToBlunt(_inputPath, quality));
                    break;
                case 3:
                    await Task.Run(() => ConvertBluntToMp4(_inputPath, quality));
                    break;
            }
            StatusText.Text = "Conversion complete.";
        }
        catch (OperationCanceledException) { StatusText.Text = "Cancelled."; }
        catch (Exception ex)
        {
            StatusText.Text = $"Error: {ex.Message}";
            Log($"Error: {ex}");
        }
    }

    private void ConvertWavToBlunt(string wavPath, int quality)
    {
        Log($"Reading WAV: {Path.GetFileName(wavPath)}");
        var info = WavIo.ReadHeader(wavPath);
        Log($"  {info.Channels}ch {info.SampleRate}Hz {info.Bits}-bit");

        short[] allSamples = WavIo.ReadSamples(wavPath, info, info.DataSize / 2);
        Log($"  {allSamples.Length / info.Channels} samples");

        string outPath = Path.ChangeExtension(wavPath, ".blunt");
        Log($"Encoding: {Path.GetFileName(outPath)} Q={quality}");

        using var enc = new BluntEncoder();
        enc.SetQuality((byte)quality);
        enc.SetAudio(info.Channels, info.SampleRate);

        int mbw = (320 + 15) / 16;
        int mbh = (240 + 15) / 16;
        enc.Open(outPath, 320, 240, 30, 1);

        var vframe = new BluntFrame();
        vframe.Alloc(mbw, mbh);
        Array.Fill(vframe.Y, (byte)16);
        Array.Fill(vframe.Cb, (byte)128);
        Array.Fill(vframe.Cr, (byte)128);

        int samplesPerFrame = info.SampleRate / 30;
        int sampleOffset = 0;
        int frameCount = 0;
        int totalFrames = (allSamples.Length / info.Channels) / samplesPerFrame;

        while (sampleOffset / info.Channels < allSamples.Length / info.Channels)
        {
            enc.WriteFrame(vframe, frameCount == 0);
            int remaining = (allSamples.Length - sampleOffset) / info.Channels;
            int count = Math.Min(samplesPerFrame, remaining);
            if (count <= 0) break;

            short[] chunk = new short[count * info.Channels];
            Array.Copy(allSamples, sampleOffset, chunk, 0, chunk.Length);
            enc.WriteAudioFrame(chunk, chunk.Length);

            sampleOffset += chunk.Length;
            frameCount++;
            if (frameCount % 10 == 0)
                Log($"  Frame {frameCount}/{totalFrames}");
        }

        enc.Close();
        Log($"Done! {frameCount} frames");
    }

    private void ConvertBluntToWav(string bluntPath, int quality)
    {
        Log($"Reading: {Path.GetFileName(bluntPath)}");
        using var dec = new BluntDecoder();
        dec.Open(bluntPath);
        dec.ReadHeader();
        var hdr = dec.Header;
        Log($"  {hdr.Width}x{hdr.Height} {hdr.NumFrames} frames");

        int samplesPerFrame = hdr.AudioSampleRate / 30;
        var allSamples = new System.Collections.Generic.List<short>();

        for (uint f = 0; f < hdr.NumFrames; f++)
        {
            dec.ReadFrame();
            short[] audio = dec.ReadAudioFrame(samplesPerFrame * hdr.AudioChannels);
            allSamples.AddRange(audio);
            if (f % 10 == 0) Log($"  Frame {f + 1}/{hdr.NumFrames}");
        }

        string outPath = Path.ChangeExtension(bluntPath, ".wav");
        short[] result = allSamples.ToArray();
        WavIo.Write(outPath, result, result.Length / hdr.AudioChannels, hdr.AudioChannels, hdr.AudioSampleRate, 16);
        Log($"Done! {result.Length / hdr.AudioChannels} samples -> {Path.GetFileName(outPath)}");
    }

    private void ConvertMp4ToBlunt(string mp4Path, int quality)
    {
        string outPath = Path.ChangeExtension(mp4Path, ".blunt");
        FfmpegMp4.Mp4ToBlunt(mp4Path, outPath, quality,
            log: msg => Log(msg));
    }

    private void ConvertBluntToMp4(string bluntPath, int quality)
    {
        string outPath = Path.ChangeExtension(bluntPath, ".mp4");
        FfmpegMp4.BluntToMp4(bluntPath, outPath, quality,
            log: msg => Log(msg));
    }

    private void Log(string msg)
    {
        Dispatcher.UIThread.Post(() =>
        {
            LogBox.Text += msg + "\n";
            StatusText.Text = msg;
        });
    }
}
