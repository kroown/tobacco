using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class PlayerView : UserControl
{
    private BluntDecoder? _decoder;
    private bool _isPlaying;
    private CancellationTokenSource? _playCts;
    private string? _currentPath;
    private int _currentFrame;

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
        LoadFile(files[0].Path.LocalPath);
    }

    private void LoadFile(string path)
    {
        try
        {
            _decoder?.Dispose();
            _decoder = new BluntDecoder();
            _decoder.Open(path);
            _decoder.ReadHeader();
            _currentPath = path;
            _currentFrame = 0;

            var h = _decoder.Header;
            double fps = h.FpsNum > 0 ? (double)h.FpsNum / h.FpsDen : 30.0;
            double duration = h.NumFrames / fps;
            bool hasAudio = (h.Flags & BluntConstants.FlagHasAudio) != 0;

            FileNameText.Text = Path.GetFileName(path);
            InfoText.Text = $"{h.Width}x{h.Height}  {h.NumFrames} frames  {fps:F0}fps" +
                            (hasAudio ? $"  {h.AudioSampleRate}Hz {h.AudioChannels}ch" : "");
            DurationText.Text = FormatTime(duration);
            StatusText.Text = "Loaded";
            PositionSlider.Maximum = Math.Max(1, h.NumFrames - 1);

            Placeholder.IsVisible = false;
            _decoder.Seek(0);
            ShowFrame(0);
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
        StatusText.Text = "Paused";
    }

    private void OnStop(object? sender, RoutedEventArgs e)
    {
        _isPlaying = false;
        _playCts?.Cancel();
        if (_decoder != null)
        {
            _currentFrame = 0;
            _decoder.Seek(0);
            ShowFrame(0);
        }
        StatusText.Text = "Stopped";
    }

    private async void PlayLoop(CancellationToken ct)
    {
        if (_decoder == null) return;
        var header = _decoder.Header;
        double fps = header.FpsNum > 0 ? (double)header.FpsNum / header.FpsDen : 30.0;
        int delayMs = (int)(1000.0 / fps);

        Dispatcher.UIThread.Post(() => StatusText.Text = "Playing");

        for (uint f = (uint)_currentFrame; f < header.NumFrames && !ct.IsCancellationRequested; f++)
        {
            if (!_isPlaying) { await Task.Delay(100); if (f > 0) f--; continue; }

            var frame = _decoder.ReadFrame();
            if (frame == null) break;

            _currentFrame = (int)f;
            RenderFrame(frame, header.Width, header.Height, f, fps, delayMs, ct);
        }

        _isPlaying = false;
        Dispatcher.UIThread.Post(() => StatusText.Text = "Finished");
    }

    private async void RenderFrame(BluntFrame frame, int w, int h, uint frameNum, double fps, int delayMs, CancellationToken ct)
    {
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

        uint totalFrames = frameNum;
        uint total = (uint)bmp.PixelSize.Width;
        Dispatcher.UIThread.Post(() =>
        {
            VideoImage.Source = bmp;
            double sec = frameNum / fps;
            PositionSlider.Value = frameNum;
            FrameText.Text = FormatTime(sec);
        });

        await Task.Delay(delayMs, ct);
    }

    private void ShowFrame(uint frameNum)
    {
        if (_decoder == null) return;
        var frame = _decoder.ReadFrame();
        if (frame == null) return;

        var h = _decoder.Header;
        double fps = h.FpsNum > 0 ? (double)h.FpsNum / h.FpsDen : 30.0;

        byte[] rgb = new byte[h.Width * h.Height * 3];
        ColorSpace.YCbCrToRgb(frame, rgb, h.Width, h.Height, h.Width * 3);

        var bmp = new WriteableBitmap(new PixelSize(h.Width, h.Height), new Vector(96, 96));
        using (var fb = bmp.Lock())
        {
            unsafe
            {
                var ptr = (uint*)fb.Address;
                int stride = fb.RowBytes / 4;
                for (int y = 0; y < h.Height; y++)
                {
                    for (int x = 0; x < h.Width; x++)
                    {
                        int off = (y * h.Width * 3) + x * 3;
                        ptr[y * stride + x] = (uint)(0xFF000000u | ((uint)rgb[off] << 16) | ((uint)rgb[off + 1] << 8) | rgb[off + 2]);
                    }
                }
            }
        }

        VideoImage.Source = bmp;
        double sec = frameNum / fps;
        PositionSlider.Value = frameNum;
        FrameText.Text = FormatTime(sec);
    }

    private void OnPositionChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
        if (_decoder == null || _isPlaying) return;
        uint frame = (uint)e.NewValue;
        if (frame != _currentFrame)
        {
            _decoder.Seek(frame);
            _currentFrame = (int)frame;
            ShowFrame(frame);
        }
    }

    private static string FormatTime(double seconds)
    {
        int min = (int)(seconds / 60);
        int sec = (int)seconds % 60;
        return $"{min}:{sec:D2}";
    }
}
