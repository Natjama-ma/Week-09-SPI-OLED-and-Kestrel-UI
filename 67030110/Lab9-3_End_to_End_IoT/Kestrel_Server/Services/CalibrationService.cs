namespace ESP32.Kestrel.Webserver.Services;

public class CalibrationSettings
{
    public int RawMin { get; set; } = 150;     // ค่าดิบต่ำสุด (Zero Point)
    public int RawMax { get; set; } = 3950;    // ค่าดิบสูงสุด (Span Point)
    public double ScaleMin { get; set; } = 0.0;
    public double ScaleMax { get; set; } = 100.0;
    public string Unit { get; set; } = "%";
}

public class CalibrationService
{
    private CalibrationSettings _settings = new();
    private string _currentOledMessage = "SYSTEM READY";
    private readonly object _lock = new();

    public CalibrationSettings Settings
    {
        get
        {
            lock (_lock)
            {
                return new CalibrationSettings
                {
                    RawMin = _settings.RawMin,
                    RawMax = _settings.RawMax,
                    ScaleMin = _settings.ScaleMin,
                    ScaleMax = _settings.ScaleMax,
                    Unit = _settings.Unit
                };
            }
        }
    }

    public string CurrentOledMessage
    {
        get
        {
            lock (_lock)
            {
                return _currentOledMessage;
            }
        }
    }

    public void UpdateSettings(CalibrationSettings newSettings)
    {
        // ป้องกันข้อผิดพลาดการหารด้วยศูนย์ (Divide-by-Zero Protection)
        if (newSettings.RawMax <= newSettings.RawMin)
        {
            throw new ArgumentException("RawMax ต้องมีค่ามากกว่า RawMin เสมอ!");
        }

        lock (_lock)
        {
            _settings = newSettings;
        }
    }

    public void SetOledMessage(string msg)
    {
        lock (_lock)
        {
            _currentOledMessage = msg.Length > 20 ? msg[..20] : msg;
        }
    }

    public double Compute(int rawAdc)
    {
        lock (_lock)
        {
            // Clamp ค่าให้อยู่ในช่วงที่กำหนด ป้องกันสเกลทะลัก
            int clamped = Math.Clamp(rawAdc, _settings.RawMin, _settings.RawMax);
            return ((double)(clamped - _settings.RawMin) / (_settings.RawMax - _settings.RawMin)) 
                   * (_settings.ScaleMax - _settings.ScaleMin) + _settings.ScaleMin;
        }
    }
}
