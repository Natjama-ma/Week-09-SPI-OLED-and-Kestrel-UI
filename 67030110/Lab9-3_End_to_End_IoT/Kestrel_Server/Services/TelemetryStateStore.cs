namespace ESP32.Kestrel.Webserver.Services;

public record TelemetrySnapshot(
    int RawValue,
    double CalibratedValue,
    string Unit,
    string DisplayMessage,
    string AlertLevel,
    string DataSource,
    double LatencyMs,
    double AvgLatencyMs,
    double MinLatencyMs,
    double MaxLatencyMs,
    bool IsRealTimeCompliant,
    long PacketCount,
    DateTime Timestamp
);

public class TelemetryStateStore
{
    private readonly object _lock = new();

    private int _rawValue = 2048;
    private double _calibratedValue = 50.0;
    private string _unit = "%";
    private string _displayMessage = "SYSTEM READY";
    private string _alertLevel = "NORMAL";
    private string _dataSource = "Initializing";
    private DateTime _lastUpdated = DateTime.UtcNow;

    // Latency Forensics Metrics
    private double _currentLatencyMs = 0.0;
    private double _minLatencyMs = 9999.0;
    private double _maxLatencyMs = 0.0;
    private double _sumLatencyMs = 0.0;
    private long _sampleCount = 0;

    public void Update(int rawValue, double calibratedValue, string unit, string displayMsg, string source, double latencyMs)
    {
        lock (_lock)
        {
            _rawValue = rawValue;
            _calibratedValue = Math.Round(calibratedValue, 1);
            _unit = unit;
            _displayMessage = displayMsg;
            _dataSource = source;
            _lastUpdated = DateTime.UtcNow;

            // คำนวณ Alert Level ตามเกณฑ์เปอร์เซ็นต์
            double pct = unit == "%" ? calibratedValue : (calibratedValue / 10.0);
            if (pct >= 85.0)
            {
                _alertLevel = "DANGER (HIGH)";
            }
            else if (pct >= 70.0)
            {
                _alertLevel = "WARNING";
            }
            else
            {
                _alertLevel = "NORMAL";
            }

            // คำนวณสถิติ Latency Forensics
            if (latencyMs > 0.0)
            {
                _currentLatencyMs = Math.Round(latencyMs, 2);
                _sampleCount++;
                _sumLatencyMs += latencyMs;
                if (latencyMs < _minLatencyMs) _minLatencyMs = Math.Round(latencyMs, 2);
                if (latencyMs > _maxLatencyMs) _maxLatencyMs = Math.Round(latencyMs, 2);
            }
        }
    }

    public TelemetrySnapshot GetSnapshot()
    {
        lock (_lock)
        {
            double avgLatency = _sampleCount > 0 ? Math.Round(_sumLatencyMs / _sampleCount, 2) : _currentLatencyMs;
            double minLatency = _minLatencyMs < 9990.0 ? _minLatencyMs : 0.0;
            bool compliant = _currentLatencyMs <= 100.0; // เกณฑ์ < 100 ms

            return new TelemetrySnapshot(
                _rawValue,
                _calibratedValue,
                _unit,
                _displayMessage,
                _alertLevel,
                _dataSource,
                _currentLatencyMs,
                avgLatency,
                minLatency,
                _maxLatencyMs,
                compliant,
                _sampleCount,
                _lastUpdated
            );
        }
    }
}
