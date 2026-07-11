using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class PlayerView : UserControl
{
    private BluntDecoder? _decoder;
    private bool _isPlaying;
    private CancellationTokenSource? _playCts;

    public PlayerView()
    {
        InitializeComponent();
    }

    private async void OnOpenFile(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;
        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open BLUNT file",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("BLUNT files") { Patterns = new[] { "*.blunt" } },
                new FilePickerFileType("All files") { Patterns = new[] { "*.*" } }
            }
        });
        if (files.Count == 0) return;
        string path = files[0].Path.LocalPath;
        LoadFile(path);
    }

    private void LoadFile(string path)
    {
        try
        {
            _decoder?.Dispose();
            _decoder = new BluntDecoder();
            _decoder.Open(path);
            _decoder.ReadHeader();
            FileNameText.Text = $"{Path.GetFileName(path)} - {_decoder.Header.Width}x{_decoder.Header.Height}, {_decoder.Header.NumFrames} frames, {_decoder.Header.AudioSampleRate}Hz {_decoder.Header.AudioChannels}ch";
            StatusText.Text = "File loaded. Click Play.";
        }
        catch (Exception ex)
        {
            StatusText.Text = $"Error: {ex.Message}";
        }
    }

    private void OnPlay(object? sender, RoutedEventArgs e)
    {
        if (_decoder == null) return;
        _isPlaying = true;
        _playCts?.Cancel();
        _playCts = new CancellationTokenSource();
        Task.Run(() => PlayLoop(_playCts.Token));
    }

    private void OnPause(object? sender, RoutedEventArgs e)
    {
        _isPlaying = false;
    }

    private void OnStop(object? sender, RoutedEventArgs e)
    {
        _isPlaying = false;
        _playCts?.Cancel();
        _decoder?.Seek(0);
    }

    private async void PlayLoop(CancellationToken ct)
    {
        if (_decoder == null) return;
        var header = _decoder.Header;
        double fps = header.FpsNum > 0 ? (double)header.FpsNum / header.FpsDen : 30.0;
        int delayMs = (int)(1000.0 / fps);

        _decoder.Seek(0);

        for (uint f = 0; f < header.NumFrames && !ct.IsCancellationRequested; f++)
        {
            if (!_isPlaying) { await Task.Delay(100); if (f > 0) f--; continue; }

            var frame = _decoder.ReadFrame();
            if (frame == null) break;

            int w = header.Width;
            int h = header.Height;
            byte[] rgb = new byte[w * h * 3];
            ColorSpace.YCbCrToRgb(frame, rgb, w, h, w * 3);

            var bmp = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96));
            using (var fb = bmp.Lock())
            {
                unsafe
                {
                    var ptr = (uint*)fb.Address;
                    int stride = fb.RowBytes / 4;
                    for (int y = 0; y < h; y++)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            int off = (y * w * 3) + x * 3;
                            byte r = rgb[off + 0];
                            byte g = rgb[off + 1];
                            byte b = rgb[off + 2];
                            ptr[y * stride + x] = (uint)(0xFF000000u | ((uint)r << 16) | ((uint)g << 8) | b);
                        }
                    }
                }
            }

            uint frameNum = f;
            uint totalFrames = header.NumFrames;
            Dispatcher.UIThread.Post(() =>
            {
                VideoImage.Source = bmp;
                double sec = frameNum / fps;
                int min = (int)(sec / 60);
                StatusText.Text = $"Frame {frameNum + 1}/{totalFrames} ({min}:{(int)(sec) % 60:D2})";
            });

            await Task.Delay(delayMs, ct);
        }

        _isPlaying = false;
    }

    private void OnPositionChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
    }
}
