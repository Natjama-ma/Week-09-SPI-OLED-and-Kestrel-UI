using System.Diagnostics;
using System.IO.Ports;

namespace ESP32.Kestrel.Webserver.Services;

public class SerialBridgeWorker : BackgroundService
{
    private readonly TelemetryStateStore _stateStore;
    private readonly CalibrationService _calibrationService;
    private readonly IConfiguration _config;
    private readonly ILogger<SerialBridgeWorker> _logger;
    private SerialPort? _activeSerial = null;
    private readonly object _serialLock = new();

    public SerialBridgeWorker(
        TelemetryStateStore stateStore,
        CalibrationService calibrationService,
        IConfiguration config,
        ILogger<SerialBridgeWorker> logger)
    {
        _stateStore = stateStore;
        _calibrationService = calibrationService;
        _config = config;
        _logger = logger;
    }

    // ฟังก์ชันส่งคำสั่งย้อนกลับไปยังจอ OLED ทางกายภาพผ่านพอร์ต Serial
    public bool TrySendCommandToOled(int percent, string message)
    {
        lock (_serialLock)
        {
            if (_activeSerial != null && _activeSerial.IsOpen)
            {
                try
                {
                    string command = $"SET:{percent}:{message}\n";
                    _activeSerial.Write(command);
                    return true;
                }
                catch (Exception ex)
                {
                    _logger.LogWarning("⚠️ ไม่สามารถส่งคำสั่งผ่าน Serial: {Error}", ex.Message);
                }
            }
        }
        return false;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        await Task.Yield();
        _logger.LogInformation("🚀 Kestrel Two-Way Serial Bridge Service Started.");

        string preferredPort = _config["SerialConfig:PreferredPort"] ?? "COM8";
        int baudRate = int.TryParse(_config["SerialConfig:BaudRate"], out int b) ? b : 115200;

        while (!stoppingToken.IsCancellationRequested)
        {
            string[] availablePorts = SerialPort.GetPortNames();
            string? selectedPort = null;

            if (availablePorts.Length > 0)
            {
                // ตรวจสอบพอร์ตที่กำหนด หรือเลือกพอร์ตแรกที่ไม่ใช่พอร์ตจำลอง
                if (availablePorts.Contains(preferredPort, StringComparer.OrdinalIgnoreCase))
                {
                    selectedPort = preferredPort;
                }
                else
                {
                    selectedPort = availablePorts.FirstOrDefault(p => !p.Equals("COM1", StringComparison.OrdinalIgnoreCase));
                }
            }

            if (!string.IsNullOrEmpty(selectedPort))
            {
                _logger.LogInformation("🔌 กำลังเชื่อมต่อฮาร์ดแวร์ ESP32 บนพอร์ต: {Port} @ {Baud} bps", selectedPort, baudRate);

                try
                {
                    using var serial = new SerialPort(selectedPort, baudRate)
                    {
                        ReadTimeout = 2000,
                        WriteTimeout = 1000,
                        NewLine = "\n"
                    };

                    serial.Open();
                    serial.DiscardInBuffer();
                    serial.DiscardOutBuffer();

                    lock (_serialLock)
                    {
                        _activeSerial = serial;
                    }

                    _logger.LogInformation("✅ เชื่อมต่อฮาร์ดแวร์สำเร็จบน {Port} (Live Closed-Loop Mode Active)", selectedPort);

                    var sw = Stopwatch.StartNew();

                    while (!stoppingToken.IsCancellationRequested && serial.IsOpen)
                    {
                        try
                        {
                            if (serial.BytesToRead > 0)
                            {
                                long t1Ticks = Stopwatch.GetTimestamp(); // T1: จังหวะที่ Kestrel รับข้อมูล
                                string line = serial.ReadLine().Trim();

                                int rawAdc = -1;
                                long t0Ms = 0;

                                // ตรวจสอบรูปแบบ: "ADC:<val>,T:<ms>" หรือ "ADC:<val>" หรือ "<val>"
                                if (line.StartsWith("ADC:", StringComparison.OrdinalIgnoreCase))
                                {
                                    string payload = line[4..];
                                    if (payload.Contains(",T:"))
                                    {
                                        var parts = payload.Split(",T:");
                                        int.TryParse(parts[0], out rawAdc);
                                        long.TryParse(parts[1], out t0Ms);
                                    }
                                    else
                                    {
                                        int.TryParse(payload, out rawAdc);
                                    }
                                }
                                else
                                {
                                    int.TryParse(line, out rawAdc);
                                }

                                if (rawAdc >= 0)
                                {
                                    // 1. คำนวณค่า Calibrated ผ่าน Calibration Engine
                                    double calibrated = _calibrationService.Compute(rawAdc);
                                    int calInt = (int)Math.Round(calibrated);
                                    string msg = _calibrationService.CurrentOledMessage;

                                    // 2. ส่งค่ากลับไปยัง ESP32 ทันที (Two-Way Stream)
                                    string command = $"SET:{calInt}:{msg}\n";
                                    serial.Write(command);

                                    // 3. วัดความหน่วงเวลาเชิงนิติวิทยาศาสตร์ (Latency Forensics)
                                    long t2Ticks = Stopwatch.GetTimestamp(); // T2: อัปเดตและตอบกลับสำเร็จ
                                    double latencyMs = Stopwatch.GetElapsedTime(t1Ticks, t2Ticks).TotalMilliseconds;

                                    _stateStore.Update(
                                        rawAdc,
                                        calibrated,
                                        _calibrationService.Settings.Unit,
                                        msg,
                                        $"Live Hardware ({selectedPort})",
                                        latencyMs
                                    );
                                }
                            }
                            else
                            {
                                await Task.Delay(20, stoppingToken);
                            }
                        }
                        catch (TimeoutException)
                        {
                            // Timeout ปกติเมื่อไม่มีข้อมูลเข้ามาใน 2 วินาที
                        }
                        catch (Exception ex) when (!stoppingToken.IsCancellationRequested)
                        {
                            _logger.LogWarning("⚠️ ข้อผิดพลาดในระหว่างการสตรีมพอร์ต {Port}: {Message}", selectedPort, ex.Message);
                            break;
                        }
                    }
                }
                catch (Exception ex)
                {
                    _logger.LogWarning("❌ ไม่สามารถเปิดพอร์ต {Port}: {Message}", selectedPort, ex.Message);
                }
                finally
                {
                    lock (_serialLock)
                    {
                        _activeSerial = null;
                    }
                }
            }

            // หากไม่พบพอร์ตฮาร์ดแวร์จริง ให้เข้าสู่ Safe Simulation Mode อัตโนมัติ
            if (!stoppingToken.IsCancellationRequested)
            {
                _logger.LogInformation("⚠️ ไม่พบพอร์ตฮาร์ดแวร์จริง รันในโหมดจำลอง (Autonomous Simulation Fallback)...");

                float simAngle = 0.0f;
                while (!stoppingToken.IsCancellationRequested && SerialPort.GetPortNames().Length == 0)
                {
                    var t1Ticks = Stopwatch.GetTimestamp();

                    // จำลองค่า Potentiometer หมุนไป-มา 0 ถึง 4095
                    int simRaw = (int)((Math.Sin(simAngle) + 1.0) / 2.0 * 3800.0) + 150;
                    simAngle += 0.08f;
                    if (simAngle > Math.PI * 2) simAngle = 0;

                    double calibrated = _calibrationService.Compute(simRaw);
                    string msg = _calibrationService.CurrentOledMessage;

                    // จำลอง Latency ประมาณ 8 - 18 ms
                    await Task.Delay(50, stoppingToken);
                    var t2Ticks = Stopwatch.GetTimestamp();
                    double latencyMs = Stopwatch.GetElapsedTime(t1Ticks, t2Ticks).TotalMilliseconds % 25.0 + 8.0;

                    _stateStore.Update(
                        simRaw,
                        calibrated,
                        _calibrationService.Settings.Unit,
                        msg,
                        "Simulation Mode (No Hardware)",
                        latencyMs
                    );
                }

                await Task.Delay(1000, stoppingToken);
            }
        }
    }
}
