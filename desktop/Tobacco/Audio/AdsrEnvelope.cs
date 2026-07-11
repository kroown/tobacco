namespace Tobacco.Audio;

public enum AdsrStage { Idle, Attack, Decay, Sustain, Release }

public class AdsrEnvelope
{
    public double AttackTime { get; set; } = 0.01;
    public double DecayTime { get; set; } = 0.1;
    public double SustainLevel { get; set; } = 0.7;
    public double ReleaseTime { get; set; } = 0.2;

    private AdsrStage _stage = AdsrStage.Idle;
    private double _level;
    private double _time;

    public void NoteOn()
    {
        _stage = AdsrStage.Attack;
        _level = 0.0;
        _time = 0.0;
    }

    public void NoteOff()
    {
        _stage = AdsrStage.Release;
        _time = 0.0;
    }

    public double Next(double dt)
    {
        _time += dt;
        switch (_stage)
        {
            case AdsrStage.Attack:
                _level = AttackTime > 0 ? _time / AttackTime : 1.0;
                if (_level >= 1.0) { _level = 1.0; _stage = AdsrStage.Decay; _time = 0.0; }
                break;
            case AdsrStage.Decay:
                _level = DecayTime > 0 ? 1.0 - (1.0 - SustainLevel) * (_time / DecayTime) : SustainLevel;
                if (_level <= SustainLevel) { _level = SustainLevel; _stage = AdsrStage.Sustain; _time = 0.0; }
                break;
            case AdsrStage.Sustain:
                _level = SustainLevel;
                break;
            case AdsrStage.Release:
                _level = ReleaseTime > 0 ? SustainLevel * (1.0 - _time / ReleaseTime) : 0.0;
                if (_level <= 0.0) { _level = 0.0; _stage = AdsrStage.Idle; }
                break;
            default:
                _level = 0.0;
                break;
        }
        return _level;
    }

    public bool IsActive => _stage != AdsrStage.Idle;
}
