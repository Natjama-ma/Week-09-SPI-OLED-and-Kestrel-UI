using ESP32.Kestrel.Webserver.Services;

var builder = WebApplication.CreateBuilder(args);

// ลงทะเบียน Services
builder.Services.AddSingleton<CalibrationService>();
builder.Services.AddSingleton<TelemetryStateStore>();
builder.Services.AddSingleton<SerialBridgeWorker>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<SerialBridgeWorker>());

var app = builder.Build();

// รองรับการเสิร์ฟไฟล์ Static Web Dashboard จากโฟลเดอร์ wwwroot
app.UseFileServer();

// ==============================================================================
// REST MINIMAL API ENDPOINTS (Lab 9.2 & Lab 9.3)
// ==============================================================================

// 1. GET /api/telemetry : สตรีมข้อมูล Telemetry ล่าสุด
app.MapGet("/api/telemetry", (TelemetryStateStore stateStore) =>
{
    var snapshot = stateStore.GetSnapshot();
    return Results.Ok(new
    {
        raw = snapshot.RawValue,
        calibrated = snapshot.CalibratedValue,
        unit = snapshot.Unit,
        displayMsg = snapshot.DisplayMessage,
        alertLevel = snapshot.AlertLevel,
        dataSource = snapshot.DataSource,
        latencyMs = snapshot.LatencyMs,
        avgLatencyMs = snapshot.AvgLatencyMs,
        isRealTimeCompliant = snapshot.IsRealTimeCompliant,
        packetCount = snapshot.PacketCount,
        timestamp = snapshot.Timestamp.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
    });
});

// 2. POST /api/potentiometer/calibrate : ตั้งค่าพารามิเตอร์การปรับเทียบ (Two-Point Linear Calibration)
app.MapPost("/api/potentiometer/calibrate", (
    CalibrationSettings newSettings, 
    CalibrationService cal, 
    SerialBridgeWorker serialWorker,
    TelemetryStateStore stateStore) =>
{
    try
    {
        cal.UpdateSettings(newSettings);
        cal.SetOledMessage("CALIBRATED OK");

        // ส่งคำสั่งอัปเดตไปยังจอ OLED ทันที
        var snapshot = stateStore.GetSnapshot();
        int currentCal = (int)Math.Round(cal.Compute(snapshot.RawValue));
        serialWorker.TrySendCommandToOled(currentCal, "CALIBRATED OK");

        return Results.Ok(new 
        { 
            status = "success", 
            message = "Calibration updated successfully",
            settings = cal.Settings 
        });
    }
    catch (ArgumentException ex)
    {
        return Results.BadRequest(new { status = "error", message = ex.Message });
    }
});

// 3. POST /api/oled/message : สั่งแสดงผลข้อความบนจอ OLED ทางกายภาพ (Zone 3 Footer)
app.MapPost("/api/oled/message", (
    DisplayMessageRequest req, 
    CalibrationService cal, 
    SerialBridgeWorker serialWorker,
    TelemetryStateStore stateStore) =>
{
    if (string.IsNullOrWhiteSpace(req.Message))
    {
        return Results.BadRequest(new { status = "error", message = "ข้อความต้องไม่ว่างเปล่า" });
    }

    cal.SetOledMessage(req.Message);

    // ยิงแพ็กเก็ตข้อความตรงสู่ ESP32 Serial
    var snapshot = stateStore.GetSnapshot();
    int currentCal = (int)Math.Round(cal.Compute(snapshot.RawValue));
    serialWorker.TrySendCommandToOled(currentCal, cal.CurrentOledMessage);

    return Results.Ok(new 
    { 
        status = "success", 
        current = cal.CurrentOledMessage 
    });
});

// 4. GET /api/latency : ตรวจสอบข้อมูลสถิติเชิงนิติวิทยาศาสตร์ (Latency Forensics)
app.MapGet("/api/latency", (TelemetryStateStore stateStore) =>
{
    var snapshot = stateStore.GetSnapshot();
    return Results.Ok(new
    {
        currentLatencyMs = snapshot.LatencyMs,
        avgLatencyMs = snapshot.AvgLatencyMs,
        minLatencyMs = snapshot.MinLatencyMs,
        maxLatencyMs = snapshot.MaxLatencyMs,
        isRealTimeCompliant = snapshot.IsRealTimeCompliant,
        benchmarkTargetMs = 100.0,
        sampleCount = snapshot.PacketCount,
        verdict = snapshot.IsRealTimeCompliant ? "PASS (<100ms Real-Time Compliant)" : "WARNING (Latency > 100ms)"
    });
});

app.Run();
