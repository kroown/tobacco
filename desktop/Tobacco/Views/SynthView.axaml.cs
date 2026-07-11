using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Tobacco.Audio;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class SynthView : UserControl
{
    private SynthEngine _synth;
    private readonly Dictionary<string, double> _keyFreqs = new();
    private readonly Dictionary<object, string> _keyNames = new();

    public SynthView()
    {
        InitializeComponent();
        _synth = new SynthEngine();
        _synth.Init();
        BuildKeyboard();
    }

    private void BuildKeyboard()
    {
        var canvas = KeyboardCanvas;
        canvas.Background = new SolidColorBrush(Color.Parse("#0a0a18"));

        string[] whiteNotes = { "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5" };
        double[] whiteFreqs = { 261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25 };
        string[] blackNotes = { "C#4", "D#4", null, "F#4", "G#4", "A#4", null };
        double[] blackFreqs = { 277.18, 311.13, 0, 369.99, 415.30, 466.16, 0 };

        double w = 60;
        double h = 120;
        double bw = 36;
        double bh = 72;

        for (int i = 0; i < whiteNotes.Length; i++)
        {
            double x = i * w;
            var border = new Border
            {
                Width = w - 2,
                Height = h,
                Background = new SolidColorBrush(Color.Parse("#d8d8e8")),
                CornerRadius = new CornerRadius(0, 0, 6, 6),
                BorderBrush = new SolidColorBrush(Color.Parse("#a0a0b8")),
                BorderThickness = new Thickness(0.5)
            };
            _keyNames[border] = whiteNotes[i];
            border.PointerPressed += OnKeyPointerDown;
            border.PointerReleased += OnKeyPointerUp;
            Canvas.SetLeft(border, x + 1);
            canvas.Children.Add(border);

            var label = new TextBlock
            {
                Text = whiteNotes[i],
                FontSize = 10,
                Foreground = new SolidColorBrush(Color.Parse("#606080")),
                HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, 0, 6)
            };
            Canvas.SetLeft(label, x);
            Canvas.SetTop(label, h - 28);
            canvas.Children.Add(label);

            _keyFreqs[whiteNotes[i]] = whiteFreqs[i];
        }

        int[] blackPositions = { 0, 1, 3, 4, 5 };
        for (int i = 0; i < blackNotes.Length; i++)
        {
            if (blackNotes[i] == null) continue;
            int idx = i;
            double x = (idx * w) + w - bw / 2;
            var border = new Border
            {
                Width = bw,
                Height = bh,
                Background = new SolidColorBrush(Color.Parse("#1a1a2e")),
                CornerRadius = new CornerRadius(0, 0, 4, 4),
                BorderBrush = new SolidColorBrush(Color.Parse("#303048")),
                BorderThickness = new Thickness(0.5)
            };
            _keyNames[border] = blackNotes[i];
            border.PointerPressed += OnKeyPointerDown;
            border.PointerReleased += OnKeyPointerUp;
            border.ZIndex = 1;

            _keyFreqs[blackNotes[i]] = blackFreqs[i];
        }
    }

    private void OnKeyPointerDown(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not StyledElement elem) return;
        if (!_keyNames.TryGetValue(elem, out string? name)) return;
        if (_keyFreqs.TryGetValue(name, out double freq))
        {
            _synth.NoteOn(freq);
            if (sender is Border b)
                b.Background = new SolidColorBrush(Color.Parse("#e67e22"));
        }
    }

    private void OnKeyPointerUp(object? sender, PointerReleasedEventArgs e)
    {
        if (sender is not StyledElement elem) return;
        if (!_keyNames.TryGetValue(elem, out string? name)) return;
        if (_keyFreqs.TryGetValue(name, out double freq))
        {
            _synth.NoteOff(freq);
            bool isBlack = name.Contains("#");
            if (sender is Border b)
                b.Background = new SolidColorBrush(Color.Parse(isBlack ? "#1a1a2e" : "#d8d8e8"));
        }
    }

    private void OnNoteClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not string freqStr) return;
        if (double.TryParse(freqStr, out double freq))
            _synth.NoteOn(freq);
    }

    private void OnAllOff(object? sender, RoutedEventArgs e) => _synth.AllNotesOff();

    private void OnWaveformChanged(object? sender, SelectionChangedEventArgs e)
    {
        _synth.Waveform = WaveformCombo.SelectedIndex switch
        {
            0 => WaveformType.Sine,
            1 => WaveformType.Square,
            2 => WaveformType.Saw,
            3 => WaveformType.Triangle,
            4 => WaveformType.Noise,
            _ => WaveformType.Sine
        };
    }

    private void OnVolumeChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
        _synth.MasterVolume = e.NewValue / 100.0;
        VolumeText.Text = $"{(int)e.NewValue}%";
    }

    private void OnAdsrChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
        _synth.AdsrTemplate.AttackTime = AttackSlider.Value / 1000.0;
        _synth.AdsrTemplate.DecayTime = DecaySlider.Value / 1000.0;
        _synth.AdsrTemplate.SustainLevel = SustainSlider.Value / 100.0;
        _synth.AdsrTemplate.ReleaseTime = ReleaseSlider.Value / 1000.0;
        AttackText.Text = $"{(int)AttackSlider.Value}ms";
        DecayText.Text = $"{(int)DecaySlider.Value}ms";
        SustainText.Text = $"{(int)SustainSlider.Value}%";
        ReleaseText.Text = $"{(int)ReleaseSlider.Value}ms";
    }

    private void OnFilterTypeChanged(object? sender, SelectionChangedEventArgs e)
    {
        _synth.Filter.Type = FilterTypeCombo.SelectedIndex switch
        {
            0 => FilterType.Lowpass,
            1 => FilterType.Highpass,
            2 => FilterType.Bandpass,
            3 => FilterType.Notch,
            _ => FilterType.Lowpass
        };
    }

    private void OnFilterChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
        _synth.FilterEnabled = FilterEnabledCheck.IsChecked == true;
        _synth.Filter.Frequency = FilterFreqSlider.Value;
        _synth.Filter.Resonance = FilterResSlider.Value / 100.0;
        FilterFreqText.Text = $"{(int)FilterFreqSlider.Value} Hz";
    }

    private void OnLfoChanged(object? sender, RangeBaseValueChangedEventArgs e)
    {
        _synth.FilterLfo.Rate = LfoRateSlider.Value / 100.0;
        _synth.FilterLfo.Depth = LfoDepthSlider.Value;
        LfoRateText.Text = $"{LfoRateSlider.Value / 100.0:F1} Hz";
        LfoDepthText.Text = $"{(int)LfoDepthSlider.Value}";
    }

    private async void OnExportWav(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;
        var file = await topLevel.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export synth as WAV",
            SuggestedFileName = "synth_output.wav",
            FileTypeChoices = new[] { new FilePickerFileType("WAV files") { Patterns = new[] { "*.wav" } } }
        });
        if (file == null) return;

        SynthStatus.Text = "Rendering 2 seconds of A440...";
        await Task.Run(() =>
        {
            _synth.NoteOn(440.0);
            short[] samples = _synth.RenderBuffer(44100 * 2, 1);
            _synth.NoteOff(440.0);
            WavIo.Write(file.Path.LocalPath, samples, samples.Length, 1, 44100, 16);
        });
        SynthStatus.Text = $"Exported {Path.GetFileName(file.Path.LocalPath)}";
    }
}
