using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.IO;
using System.Threading.Tasks;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class ConverterView : UserControl
{
    private string? _inputPath;

    public ConverterView()
    {
        InitializeComponent();
        QualitySlider.ValueChanged += (s, e) =>
        {
            QualityText.Text = ((int)e.NewValue).ToString();
        };
    }

    private async void OnOpenFile(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;
        bool isWavToBlunt = ModeCombo.SelectedIndex == 0;
        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = isWavToBlunt ? "Select WAV file" : "Select BLUNT file",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                isWavToBlunt
                    ? new FilePickerFileType("WAV files") { Patterns = new[] { "*.wav" } }
                    : new FilePickerFileType("BLUNT files") { Patterns = new[] { "*.blunt" } }
            }
        });
        if (files.Count == 0) return;
        _inputPath = files[0].Path.LocalPath;
        InputFileText.Text = Path.GetFileName(_inputPath);
    }

    private async void OnConvert(object? sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_inputPath))
        {
            StatusText.Text = "Please select a file first.";
            return;
        }

        bool isWavToBlunt = ModeCombo.SelectedIndex == 0;
        int quality = (int)QualitySlider.Value;

        try
        {
            if (isWavToBlunt)
                await Task.Run(() => ConvertWavToBlunt(_inputPath, quality));
            else
                await Task.Run(() => ConvertBluntToWav(_inputPath, quality));
            StatusText.Text = "Conversion complete!";
        }
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
        Log($"  {info.Channels}ch {info.SampleRate}Hz {info.Bits}-bit {info.DataSize} bytes");

        short[] allSamples = WavIo.ReadSamples(wavPath, info, info.DataSize / 2);
        Log($"  Read {allSamples.Length / info.Channels} samples");

        string outPath = Path.ChangeExtension(wavPath, ".blunt");
        Log($"Encoding to: {Path.GetFileName(outPath)} (quality={quality})");

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
            enc.WriteAudioFrame(chunk, count * info.Channels);

            sampleOffset += count * info.Channels;
            frameCount++;
            if (frameCount % 10 == 0)
                Log($"  Frame {frameCount}/{totalFrames}");
        }

        enc.Close();
        Log($"Done! {frameCount} frames written to {Path.GetFileName(outPath)}");
    }

    private void ConvertBluntToWav(string bluntPath, int quality)
    {
        Log($"Reading BLUNT: {Path.GetFileName(bluntPath)}");
        using var dec = new BluntDecoder();
        dec.Open(bluntPath);
        dec.ReadHeader();
        var hdr = dec.Header;
        Log($"  {hdr.Width}x{hdr.Height} {hdr.NumFrames} frames {hdr.AudioSampleRate}Hz {hdr.AudioChannels}ch");

        string outPath = Path.ChangeExtension(bluntPath, ".wav");
        Log($"Decoding to: {Path.GetFileName(outPath)}");

        int samplesPerFrame = hdr.AudioSampleRate / 30;
        var allSamples = new System.Collections.Generic.List<short>();

        for (uint f = 0; f < hdr.NumFrames; f++)
        {
            dec.ReadFrame();
            short[] audio = dec.ReadAudioFrame(samplesPerFrame * hdr.AudioChannels);
            allSamples.AddRange(audio);
            if (f % 10 == 0)
                Log($"  Frame {f + 1}/{hdr.NumFrames}");
        }

        short[] result = allSamples.ToArray();
        WavIo.Write(outPath, result, result.Length / hdr.AudioChannels, hdr.AudioChannels, hdr.AudioSampleRate, 16);
        Log($"Done! {result.Length / hdr.AudioChannels} samples written to {Path.GetFileName(outPath)}");
    }

    private void Log(string msg)
    {
        Dispatcher.UIThread.Post(() =>
        {
            LogBox.Text += msg + Environment.NewLine;
            StatusText.Text = msg;
        });
    }
}
