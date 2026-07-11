using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Tobacco;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        ShowView(PlayerView);
    }

    private void ShowView(UserControl view)
    {
        PlayerView.IsVisible = view == PlayerView;
        ConverterView.IsVisible = view == ConverterView;
        SynthView.IsVisible = view == SynthView;

        NavPlayer.Classes.Clear();
        NavConverter.Classes.Clear();
        NavSynth.Classes.Clear();

        if (view == PlayerView) NavPlayer.Classes.Add("accent");
        else if (view == ConverterView) NavConverter.Classes.Add("accent");
        else if (view == SynthView) NavSynth.Classes.Add("accent");
    }

    private void OnNavPlayer(object? sender, RoutedEventArgs e) => ShowView(PlayerView);
    private void OnNavConverter(object? sender, RoutedEventArgs e) => ShowView(ConverterView);
    private void OnNavSynth(object? sender, RoutedEventArgs e) => ShowView(SynthView);
}
