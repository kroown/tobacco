using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System;
using System.IO;
using System.Threading.Tasks;
using Tobacco.Audio;
using Tobacco.Codec;

namespace Tobacco.Views;

public partial class SynthView : UserControl
{
    private SynthEngine _synth;

    public SynthView()
    {
        InitializeComponent();
        _synth = new SynthEngine();
        _synth.Init();
    }

    private void OnPlayC4(object sender, RoutedEventArgs e) => _synth.NoteOn(261.63);
    private void OnPlayE4(object sender, RoutedEventArgs e) => _synth.NoteOn(329.63);
    private void OnPlayG4(object sender, RoutedEventArgs e) => _synth.NoteOn(392.00);
    private void OnAllOff(object sender, RoutedEventArgs e) => _synth.AllNotesOff();

    private void OnWaveformChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_synth == null) return;
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

    private void OnVolumeChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_synth != null)
            _synth.MasterVolume = e.NewValue / 100.0;
        VolumeText.Text = $"{(int)e.NewValue}%";
    }

    private void OnAdsrChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_synth == null) return;
        _synth.AdsrTemplate.AttackTime = AttackSlider.Value / 1000.0;
        _synth.AdsrTemplate.DecayTime = DecaySlider.Value / 1000.0;
        _synth.AdsrTemplate.SustainLevel = SustainSlider.Value / 100.0;
        _synth.AdsrTemplate.ReleaseTime = ReleaseSlider.Value / 1000.0;
        AttackText.Text = $"{(int)AttackSlider.Value}ms";
        DecayText.Text = $"{(int)DecaySlider.Value}ms";
        SustainText.Text = $"{(int)SustainSlider.Value}%";
        ReleaseText.Text = $"{(int)ReleaseSlider.Value}ms";
    }

    private void OnFilterChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_synth == null) return;
        _synth.FilterEnabled = FilterEnabledCheck.IsChecked == true;
        _synth.Filter.Type = FilterTypeCombo.SelectedIndex switch
        {
            0 => FilterType.Lowpass,
            1 => FilterType.Highpass,
            2 => FilterType.Bandpass,
            3 => FilterType.Notch,
            _ => FilterType.Lowpass
        };
        _synth.Filter.Frequency = FilterFreqSlider.Value;
        _synth.Filter.Resonance = FilterResSlider.Value / 100.0;
        FilterFreqText.Text = $"{(int)FilterFreqSlider.Value} Hz";
    }

    private void OnLfoChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_synth == null) return;
        _synth.FilterLfo.Rate = LfoRateSlider.Value / 100.0;
        _synth.FilterLfo.Depth = LfoDepthSlider.Value;
        LfoRateText.Text = $"{LfoRateSlider.Value / 100.0:F1} Hz";
        LfoDepthText.Text = $"{(int)LfoDepthSlider.Value}";
    }

    private async void OnExportWav(object sender, RoutedEventArgs e)
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

        SynthStatus.Text = "Rendering 2 seconds...";
        await Task.Run(() =>
        {
            _synth.NoteOn(440.0);
            short[] samples = _synth.RenderBuffer(44100 * 2, 1);
            _synth.NoteOff(440.0);
            WavIo.Write(file.Path.LocalPath, samples, samples.Length, 1, 44100, 16);
        });
        SynthStatus.Text = $"Exported to {Path.GetFileName(file.Path.LocalPath)}";
    }
}
