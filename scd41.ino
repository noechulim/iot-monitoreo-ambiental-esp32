#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SensirionI2cScd4x.h>

// =========================
// WIFI
// =========================
const char* ssid = "Pongan Caifanes";
const char* password = "coso1234";

// =========================
// BROKER MQTT (Mosquitto)
// =========================
const char* mqtt_server = "10.134.55.144";
const int mqtt_port = 1883;
const char* mqtt_topic = "scd41/ambiente";

// =========================
// OLED & SENSOR SCD41
// =========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
SensirionI2cScd4x sensor;

// =========================
// LEDs (Semáforo de calidad)
// =========================
const int LED_VERDE = 25;
const int LED_AMARILLO = 26;
const int LED_ROJO = 27;

// =========================
// CLIENTE MQTT & TEMPORIZADOR
// =========================
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long ultimoEnvio = 0;
const unsigned long intervalo = 30000; // Intervalo de muestreo (5 segundos)

void conectarWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado exitosamente");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());
}

void conectarMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando a Mosquitto...");
    String clientId = "ESP32_SCD41_" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println(" conectado exitosamente");
    } else {
      Serial.print(" fallo, rc=");
      Serial.print(client.state());
      Serial.println(". Reintentando en 3 segundos...");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Bus I2C (SDA = 21, SCL = 22)
  Wire.begin(21, 22);

  // Configuración de pines LED
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);

  // Inicialización Pantalla OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error: No se encontró la pantalla OLED");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Sistema SCD41 + MQTT");
  display.setCursor(0, 15);
  display.println("Iniciando...");
  display.display();

  // Inicialización del SCD41
  sensor.begin(Wire, 0x62);

  uint16_t error;
  char errorMessage[256];

  error = sensor.stopPeriodicMeasurement();
  delay(500);

  // Compensar el auto-calentamiento del ESP32/OLED (ajustable según tu entorno)
  sensor.setTemperatureOffset(3.5);

  error = sensor.startPeriodicMeasurement();
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.print("Error iniciando mediciones: ");
    Serial.println(errorMessage);
    while (true) delay(1000);
  }

  Serial.println("Sensor SCD41 inicializado correctamente.");

  // Inicialización de red
  conectarWiFi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();

  unsigned long ahora = millis();

  if (ahora - ultimoEnvio >= intervalo) {
    bool dataReady = false;
    uint16_t error;
    char errorMessage[256];

    // Verificar si el SCD41 tiene datos listos antes de leer
    error = sensor.getDataReadyStatus(dataReady);
    if (error) {
      errorToString(error, errorMessage, sizeof(errorMessage));
      Serial.print("Error al verificar estado: ");
      Serial.println(errorMessage);
      return;
    }

    if (!dataReady) {
      return; // El ciclo de medición sigue en proceso
    }

    ultimoEnvio = ahora;

    uint16_t co2 = 0;
    float temperatura = 0.0f;
    float humedad = 0.0f;

    error = sensor.readMeasurement(co2, temperatura, humedad);
    if (error) {
      errorToString(error, errorMessage, sizeof(errorMessage));
      Serial.print("Error en lectura: ");
      Serial.println(errorMessage);
      return;
    }

    // =========================
    // ESTADO Y SEMÁFORO LED
    // =========================
    String estado;
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_ROJO, LOW);

    if (co2 < 800) {
      estado = "BUENA";
      digitalWrite(LED_VERDE, HIGH);
    } else if (co2 <= 1200) {
      estado = "REGULAR";
      digitalWrite(LED_AMARILLO, HIGH);
    } else {
      estado = "MALA";
      digitalWrite(LED_ROJO, HIGH);
    }

    // =========================
    // PANTALLA OLED
    // =========================
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("CO2: ");
    display.print(co2);
    display.println(" ppm");

    display.setCursor(0, 15);
    display.print("Temp: ");
    display.print(temperatura, 1);
    display.println(" C");

    display.setCursor(0, 30);
    display.print("Hum: ");
    display.print(humedad, 1);
    display.println(" %");

    display.setCursor(0, 45);
    display.print("Estado: ");
    display.println(estado);

    display.display();

    // =========================
    // EMPAQUETADO JSON Y MQTT
    // =========================
    String payload = "{";
    payload += "\"co2\":" + String(co2) + ",";
    payload += "\"temperatura\":" + String(temperatura, 1) + ",";
    payload += "\"humedad\":" + String(humedad, 1) + ",";
    payload += "\"calidad\":\"" + estado + "\"";
    payload += "}";

    client.publish(mqtt_topic, payload.c_str());

    // Salida serie para depuración local
    Serial.print("Publicado en [");
    Serial.print(mqtt_topic);
    Serial.print("]: ");
    Serial.println(payload);
  }
}