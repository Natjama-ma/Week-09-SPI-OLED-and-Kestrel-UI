<!DOCTYPE html>
<html lang="th">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Closed-Loop IoT Dashboard</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #0f172a;
            color: #f8fafc;
            padding: 25px;
        }

        .card {
            background: #1e293b;
            border-radius: 12px;
            padding: 20px;
            max-width: 600px;
            margin: 0 auto;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5);
        }

        h2 {
            text-align: center;
            color: #38bdf8;
            margin-top: 0;
        }

        .grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin-bottom: 20px;
        }

        .stat-box {
            background: #334155;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }

        .stat-val {
            font-size: 28px;
            font-weight: bold;
            color: #4ade80;
        }

        .stat-label {
            font-size: 13px;
            color: #94a3b8;
        }

        .gauge-svg {
            width: 100%;
            height: 50px;
            background: #0f172a;
            border-radius: 6px;
            border: 1px solid #475569;
        }

        .btn {
            background: #0284c7;
            color: white;
            border: none;
            padding: 10px 18px;
            border-radius: 6px;
            cursor: pointer;
            font-weight: bold;
        }

        .btn:hover {
            background: #0369a1;
        }

        input[type="text"] {
            width: calc(100% - 110px);
            padding: 9px;
            border-radius: 6px;
            border: 1px solid #475569;
            background: #0f172a;
            color: white;
        }
    </style>
</head>

<body>
    <div class="card">
        <h2>Closed-Loop IoT System Dashboard</h2>

        <div class="grid">
            <div class="stat-box">
                <div class="stat-label">RAW ADC (ESP32)</div>
                <div class="stat-val" id="lbl-raw">0</div>
            </div>
            <div class="stat-box">
                <div class="stat-label">CALIBRATED VALUE</div>
                <div class="stat-val"><span id="lbl-cal">0.0</span> <span id="lbl-unit"
                        style="font-size: 16px;">%</span></div>
            </div>
        </div>

        <p style="margin-bottom: 5px; font-size: 14px;">Dynamic SVG Bar Gauge (Web Replica of OLED):</p>
        <svg class="gauge-svg" id="svg-gauge">
            <rect x="5" y="10" width="0" height="30" fill="#38bdf8" id="bar-fill" rx="4"></rect>
            <text x="50%" y="30" fill="white" font-size="14" font-weight="bold" text-anchor="middle"
                id="bar-text">0%</text>
        </svg>

        <hr style="border-color: #334155; margin: 25px 0;">

        <h3>Interactive OLED Remote Control</h3>
        <div style="display: flex; gap: 10px;">
            <input type="text" id="txt-msg" placeholder="พิมพ์ข้อความส่งเข้าจอ OLED..." maxlength="16">
            <button class="btn" onclick="sendOledMessage()">ส่งข้อความ</button>
        </div>
        <p style="font-size: 12px; color: #94a3b8; margin-top: 8px;">สถานะการส่ง: <span id="msg-status">-</span></p>
    </div>

    <script>
        async function fetchTelemetry() {
            try {
                const res = await fetch('/api/telemetry');
                if (res.ok) {
                    const data = await res.json();
                    document.getElementById('lbl-raw').innerText = data.raw;
                    document.getElementById('lbl-cal').innerText = data.calibrated;
                    document.getElementById('lbl-unit').innerText = data.unit;

                    // ปรับความกว้างของ SVG Bar Gauge (ความกว้างหน้าต่างจริง)
                    const svgWidth = document.getElementById('svg-gauge').clientWidth - 10;
                    const fillWidth = Math.max(0, Math.min(svgWidth, (data.calibrated / 100) * svgWidth));
                    document.getElementById('bar-fill').setAttribute('width', fillWidth);
                    document.getElementById('bar-text').textContent = data.calibrated + ' ' + data.unit;
                }
            } catch (err) {
                console.error("Telemetry fetch error:", err);
            }
        }

        async function sendOledMessage() {
            const input = document.getElementById('txt-msg');
            const status = document.getElementById('msg-status');
            if (!input.value.trim()) return;

            status.innerText = "กำลังส่ง...";
            try {
                const res = await fetch('/api/oled/message', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ message: input.value })
                });
                if (res.ok) {
                    status.innerText = "ส่งข้อความสำเร็จ!";
                    input.value = "";
                } else {
                    status.innerText = "เกิดข้อผิดพลาดในการส่ง!";
                }
            } catch (e) {
                status.innerText = "ไม่สามารถติดต่อเซิร์ฟเวอร์ได้";
            }
        }

        // ดึงข้อมูล Real-time ทุกๆ 50 ms (20 Hz สอดคล้องกับรอบส่งของ ESP32)
        setInterval(fetchTelemetry, 50);
    </script>
</body>

</html>