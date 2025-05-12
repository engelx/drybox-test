#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// CONFIGURACIÓN DEL SENSOR
#define DHTPIN 4             // Pin del sensor DHT
#define DHTTYPE DHT22        // O DHT11 según tu hardware
DHT dht(DHTPIN, DHTTYPE);

// CONFIGURACIÓN WIFI
const char* ssid = "*****";
const char* password = "****";

// CONFIGURACIÓN DEL FAN (PWM a 25kHz, 8 bits)
const int fanPin = 18;
const int fanChannel = 0;
const int pwmFreq = 25000;
const int pwmResolution = 8;
int fanPercent = 1; // Valor actual del fan

// CONFIGURACIÓN DEL LED (IO2)
const int ledPin = 2;
enum BlinkMode { STARTUP, NORMAL };
BlinkMode blinkMode = STARTUP;
unsigned long lastBlinkTime = 0;
bool ledState = false;
int startupBlinks = 0;  // Contabiliza ciclos (on+off) en startup

// SERVIDOR HTTP
WebServer server(80);

// HISTÓRICO DE MEDICIONES (24h, 1 registro/minuto = 1440 registros)
// Para optimizar memoria, se usa una estructura compacta:
// - temperature: en décimas de grado (por ejemplo 253 = 25.3 °C) (int16_t)
// - humidity: porcentaje (0-100) (uint8_t)
#define MAX_RECORDS 1440

struct __attribute__((packed)) Measurement {
  int16_t temperature;  // temperatura en décimas
  uint8_t humidity;     // humedad en %
};

Measurement history[MAX_RECORDS];
uint16_t historyIndex = 0; // índice circular
uint16_t historyCount = 0; // número de mediciones almacenadas

// Variables para promedio de 12 lecturas (una cada 5 segundos ==> 1 minuto)
const uint8_t samplesPerMinute = 12;
uint8_t sampleCount = 0;
uint32_t tempAccumulator = 0; // acumulador en décimas
uint16_t humAccumulator = 0;

unsigned long sensorPrevMillis = 0;
const unsigned long sensorInterval = 5000; // 5 segundos

// ──────────────────────────────
// Función: Actualiza el parpadeo del LED
// ──────────────────────────────
void updateLedBlink() {
  unsigned long currentMillis = millis();
  
  if(blinkMode == STARTUP) {
    // Parpadeo de inicio: 200ms on, 200ms off, 3 ciclos completos
    if (currentMillis - lastBlinkTime >= 200) {
      lastBlinkTime = currentMillis;
      ledState = !ledState;
      digitalWrite(ledPin, ledState ? HIGH : LOW);
      if (!ledState) { // finalizó un ciclo (on+off)
        startupBlinks++;
        if (startupBlinks >= 3) {
          blinkMode = NORMAL;
          lastBlinkTime = currentMillis;
          ledState = false;
          digitalWrite(ledPin, LOW);
        }
      }
    }
  }
  else if(blinkMode == NORMAL) {
    // Parpadeo continuo a 2 Hz: 250ms on, 250ms off
    if (currentMillis - lastBlinkTime >= 250) {
      lastBlinkTime = currentMillis;
      ledState = !ledState;
      digitalWrite(ledPin, ledState ? HIGH : LOW);
    }
  }
}

// ──────────────────────────────
// Función: Agrega la medición promedio al historial
// ──────────────────────────────
void addMeasurement(int16_t avgTemp, uint8_t avgHum) {
  history[historyIndex].temperature = avgTemp;
  history[historyIndex].humidity = avgHum;
  historyIndex = (historyIndex + 1) % MAX_RECORDS;
  if(historyCount < MAX_RECORDS)
    historyCount++;
}

// ──────────────────────────────
// Endpoint GET /data: envia los registros en formato JSON
// ──────────────────────────────
void handleData() {
  String json = "{\"fan\":" + String(fanPercent) + ",\"history\":[";
  for (uint16_t i = 0; i < historyCount; i++) {
    // Para reconstruir el orden adecuado, asumimos que el registro más antiguo está en:
    uint16_t idx = (historyIndex + MAX_RECORDS - historyCount + i) % MAX_RECORDS;
    // Convertir la temperatura a valor real (dividir entre 10) y humedad se deja como entero.
    float temp = history[idx].temperature / 10.0;
    uint8_t hum = history[idx].humidity;
    json += "{\"temp\":" + String(temp, 1) + ",\"hum\":" + String(hum) + "}";
    if (i < historyCount - 1)
      json += ",";
  }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ──────────────────────────────
// Endpoint GET /setFan?value=XX: configura el fan
// ──────────────────────────────
void handleSetFan() {
  if (server.hasArg("value")) {
    fanPercent = server.arg("value").toInt();
    if (fanPercent < 1) fanPercent = 1;
    if (fanPercent > 100) fanPercent = 100;
    int duty = map(fanPercent, 1, 100, 1, 255);
    ledcWrite(fanChannel, duty);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Fan configurado a " + String(fanPercent) + "%");
  } else {
    server.send(400, "text/plain", "Falta parámetro 'value'");
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  // Configurar PWM para el fan
  ledcSetup(fanChannel, pwmFreq, pwmResolution);
  ledcAttachPin(fanPin, fanChannel);
  int duty = map(fanPercent, 1, 100, 1, 255);
  ledcWrite(fanChannel, duty);

  // Configurar LED en IO2
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Configurar endpoints del servidor
  server.on("/data", handleData);
  server.on("/setFan", handleSetFan);
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();
  updateLedBlink();

  unsigned long currentMillis = millis();
  // Cada 5 segundos, tomar lectura del sensor
  if (currentMillis - sensorPrevMillis >= sensorInterval) {
    sensorPrevMillis = currentMillis;
    float tempRead = dht.readTemperature();
    float humRead = dht.readHumidity();
    if (!isnan(tempRead) && !isnan(humRead)) {
      // Convertir la temperatura a décimas y acumular
      int16_t tempDed = int16_t(tempRead * 10);
      tempAccumulator += tempDed;
      humAccumulator += uint8_t(humRead);
      sampleCount++;

      // Si se acumularon 12 muestras (1 minuto)
      if (sampleCount >= samplesPerMinute) {
        int16_t avgTemp = tempAccumulator / samplesPerMinute;
        uint8_t avgHum = humAccumulator / samplesPerMinute;
        addMeasurement(avgTemp, avgHum);
        // Reiniciar acumuladores y contador
        sampleCount = 0;
        tempAccumulator = 0;
        humAccumulator = 0;
        Serial.printf("Medición Promedio: Temp=%.1f C, Hum=%d%%\n", avgTemp/10.0, avgHum);
      }
    } else {
      Serial.println("Error al leer DHT");
    }
  }
}