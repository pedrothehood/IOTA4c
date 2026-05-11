#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <RD03D.h>

const char *ssid = "TP-Link_2.4GHz_0494CA";
const char *password = "pedrothehood007";

#define SENSOR_RX 16
#define SENSOR_TX 17

// RD03D radar(Serial1);
RD03D radar(SENSOR_RX, SENSOR_TX, 256000);  // RX, TX, Baudrate
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// HTML mit Canvas für die visuelle Darstellung
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<style>
body {
    background: #050505;
    color: #00ff41;
    font-family: 'Courier New', monospace;
    display: flex;
    flex-direction: column;
    align-items: center;
    margin: 0;
    overflow: hidden;
}
h1 { font-size: 1.2rem; letter-spacing: 4px; margin: 20px 0; }
canvas {
    background: #000;
    border: 1px solid #00ff41;
    box-shadow: 0 0 15px rgba(0, 255, 65, 0.2);
    border-radius: 300px 300px 0 0;
}
#status { margin-top: 10px; font-size: 0.8rem; }
</style>
    <meta charset="UTF-8">
    <title>RD-03D Radar Monitor</title>
</head>
<body>
    <h1>RD-03D REALTIME RADAR</h1>
    <canvas id="radar" width="600" height="450"></canvas>
    <div id="status">Verbinde WebSocket...</div>
    <script>
    const canvas = document.getElementById('radar');
const ctx = canvas.getContext('2d');
const status = document.getElementById('status');
const socket = new WebSocket('ws://' + window.location.hostname + '/ws');
let currentTargets = []; // Speicher für die letzten Daten
socket.onopen = () => status.innerText = "ONLINE";
socket.onclose = () => status.innerText = "OFFLINE";

// Nur Daten empfangen, NICHT zeichnen
socket.onmessage = (event) => {
    try {
        currentTargets = JSON.parse(event.data);
    } catch (e) {
        console.error("JSON Error", e);
    }
};

function drawUI() {
    ctx.strokeStyle = 'rgba(0, 255, 65, 0.15)';
    ctx.lineWidth = 1;
	  ctx.font = "10px monospace";
    // Entfernungskreise
    for(let i=1; i<=4; i++) {
        ctx.beginPath();
        ctx.arc(300, 430, i * 100, Math.PI, 2 * Math.PI);
        ctx.stroke();
        ctx.fillStyle = 'rgba(0, 255, 65, 0.3)';
        ctx.fillText(`${i}m`, 305, 430 - (i * 100));
    }
    // Achsen
    ctx.beginPath();
    ctx.moveTo(300, 430); ctx.lineTo(300, 20);
    ctx.stroke();
}

//neu// Zentrale Render-Schleife (60 FPS)
function render() {
    // Sanfter Motion Blur
    ctx.fillStyle = 'rgba(5, 5, 5, 0.2)'; 
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    
    drawUI();

    currentTargets?.targets?.forEach(t => {
        const x = 300 + (t.x / 10);
        const y = 430 - (t.y / 10);
        const isMoving = Math.abs(t.s) > 5; // Kleiner Schwellenwert für Bewegung
        const color = isMoving ? '255, 50, 50' : '50, 255, 50';

        // Zeichne Ziel
        ctx.fillStyle = `rgb(${color})`;
        ctx.beginPath();
        ctx.arc(x, y, 7, 0, Math.PI * 2);
        ctx.fill();
        
        // Glow-Effekt ohne teures shadowBlur (optional)
        ctx.strokeStyle = `rgba(${color}, 0.3)`;
        ctx.lineWidth = 3;
        ctx.stroke();
        
        ctx.fillStyle = "#fff";
        ctx.fillText(`ID:${t.id} ${t.s}cm/s`, x + 12, y);
    });

	requestAnimationFrame(render);
}
// Starte die Schleife
render();
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.print("Wifi verbinden");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());
  Serial.print("Wifi verbunden, initialisiere Sensor...");

  if (radar.initialize(RD03D::RD03DMode::MULTI_TARGET)) {
    Serial.println("Sensor erfolgreich initialisiert!");
  } else {
    Serial.println("Initialisierung fehlgeschlagen! Check RX/TX & Power.");
    while (1) {
      Serial.print(".");
      delay(1000);
    }
  }
  delay(500);  // Dem System Zeit geben, den Stack zu stabilisieren
  //Serial.println("Add handler");
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {
  // static TargetData*  ptrTarget ; //= radar.getTarget();    // get pointer to first target ( SINGLE DETECTION )
  static uint64_t next_screen_update = 0;
  static bool detected = false;

  ws.cleanupClients();
  radar.tasks();

  static uint32_t lastMsg = 0;
  unsigned long now = millis();
  static unsigned long lastWSUpdate = 0;
  if (now - lastWSUpdate > 50) {
    lastWSUpdate = now;
    if (ws.count() > 0 && ws.availableForWriteAll()) {
      bool found = false;
      // Prüfe ob ein Ziel erkannt wurde, bevor du sendest
      if (RD03D::MAX_TARGETS > 0) {
        String json = "{\"targets\":[";
        bool first = true;
        for (int i = 0; i < RD03D::MAX_TARGETS; i++) {
          TargetData *t = radar.getTarget(i);
          //Serial.println("After TargetData");
          if (t && t->isValid()) {
            if (!first) json += ",";
            json += "{\"x\":" + String(t->x)
                    + ",\"y\":" + String(t->y)
                    + ",\"s\":" + String(t->speed)
                    + ",\"id\":" + String(i) + "}";
            first = false;
          }
        }
        json += "]}";
        if (!first) {
          ws.textAll(json);
        }
      }
    }
    // lastWSUpdate = millis();
    // delay(500);  // Dem System Zeit geben, den Stack zu stabilisieren TTTTTTTTTTTTTTT
  }
}

