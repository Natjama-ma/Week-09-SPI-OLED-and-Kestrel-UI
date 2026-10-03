using System.IO.Ports;
using System.Diagnostics;

namespace ESP32.Kestrel.Webserver.Services;

public class SerialBridgeService : BackgroundService
{
    private readonly ILogger<SerialBridgeService> _logger;
    private readonly CalibrationService _calibrationService;
    private readonly IConfiguration _config;
    private SerialPort? _serialPort;

    // ตัวแปรเก็บสถานะ Telemetry ปัจจุบัน
    public int LatestRaw { get; private set; } = 0;
    public double LatestCalibrated { get; private set; } = 0.0;
    public long LatestEspUptime { get; private set; } = 0;
    public long LastRoundTripLatencyMs { get; private set; } = 0;
    public bool IsConnected => _serialPort?.IsOpen ?? false;

    public SerialBridgeService(ILogger<SerialBridgeService> logger,
                               CalibrationService calService,
                               IConfiguration config)
    {
        _logger = logger;
        _calibrationService = calService;
        _config = config;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        // อ่านค่า COM Port จาก config หรือกำหนดค่ามาตรฐาน
        string portName = _config["SerialPort:PortName"] ?? "COM24"; // <-- แก้ไขให้ตรงกับพอร์ต ESP32 ของนักศึกษา
        int baudRate = 115200;

        _logger.LogInformation("กำลังเปิดการเชื่อมต่อ Serial Port: {Port} ที่ BaudRate {Baud}", portName, baudRate);

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                if (_serialPort == null || !_serialPort.IsOpen)
                {
                    _serialPort = new SerialPort(portName, baudRate)
                    {
                        NewLine = "\n",
                        ReadTimeout = 2000,
                        WriteTimeout = 500
                    };
                    _serialPort.Open();
                    _logger.LogInformation("เชื่อมต่อพอร์ต {Port} สำเร็จ!", portName);
                }

                // อ่านข้อมูลบรรทัดใหม่จาก ESP32
                string rawLine = _serialPort.ReadLine().Trim();
                long receiveTime = Stopwatch.GetTimestamp();

                // ตรวจสอบรูปแบบ "ADC:<raw>,<uptime>"
                if (rawLine.StartsWith("ADC:"))
                {
                    string[] parts = rawLine[4..].Split(',');
                    if (int.TryParse(parts[0], out int rawValue))
                    {
                        LatestRaw = rawValue;
                        if (parts.Length > 1 && long.TryParse(parts[1], out long espUptime))
                        {
                            LatestEspUptime = espUptime;
                        }

                        // คำนวณค่า Calibrate ผ่าน Calibration Engine
                        LatestCalibrated = Math.Round(_calibrationService.Compute(rawValue), 1);

                        // ส่งคำสั่งตอบกลับไปยัง ESP32: "SET:<percent>:<message>\n"
                        int percentInt = (int)Math.Round(LatestCalibrated);
                        string msg = _calibrationService.CurrentOledMessage;
                        string txCommand = $"SET:{percentInt}:{msg}\n";

                        _serialPort.Write(txCommand);

                        // บันทึกความหน่วงเวลาโดยประมาณของ Kestrel
                        long elapsedNanos = Stopwatch.GetElapsedTime(receiveTime).Ticks * 100;
                        LastRoundTripLatencyMs = elapsedNanos / 1_000_000;
                    }
                }
            }
            catch (TimeoutException)
            {
                // หมดเวลารอข้อมูลรอบปกติ ให้วนรอบต่อไป
            }
            catch (Exception ex)
            {
                _logger.LogWarning("เกิดข้อผิดพลาดในการสื่อสาร Serial: {Msg}. กำลังลองเชื่อมต่อใหม่ใน 2 วินาที...", ex.Message);
                _serialPort?.Dispose();
                _serialPort = null;
                await Task.Delay(2000, stoppingToken);
            }

            await Task.Yield();
        }

        if (_serialPort?.IsOpen == true) _serialPort.Close();
    }
}