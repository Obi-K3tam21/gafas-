#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// PRUEBA SIN RED ELECTRICA: salida NC, modulo presumiblemente activo LOW.
// ATENCION: con NC la carga puede encenderse ante perdida de alimentacion.
// D3 (GPIO0) para SCL se conserva por cableado actual; puede afectar arranque.
#define PIN_RELE D1
#define LCD_SDA D2
#define LCD_SCL D3
#define RELE_ON HIGH
#define RELE_OFF LOW

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* SSID = "TESIS_GAFAS";
const char* PASS = "Tesis2026";
IPAddress IP_AP(192, 168, 4, 1);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress IP_GAFAS(192, 168, 4, 2);

WiFiServer servidor(80);
WiFiClient cliente;
bool tcpActivo = false;
bool autenticado = false;
bool luz = false;
String objeto = "NINGUNO";
String bufferRX = "";
unsigned long ultimoPing = 0;
unsigned long ultimaLCD = 0;
const unsigned long TIMEOUT_PING = 3000;

void actualizarLCD() {
  char linea1[17];
  char linea2[17];
  snprintf(linea1, sizeof(linea1), "CNT:%-4s L:%-3s",
           (tcpActivo && autenticado) ? "OK" : "FAIL", luz ? "ON" : "OFF");
  snprintf(linea2, sizeof(linea2), "OBJ: %-11.11s", objeto.c_str());
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  lcd.setCursor(0, 1);
  lcd.print(linea2);
}

void cerrarTCP() {
  cliente.stop();
  tcpActivo = false;
  autenticado = false;
  bufferRX = "";
  Serial.println("[TCP] DESCONECTADO");
  actualizarLCD();
}

String procesar(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "HELLO ESP32CAM") {
    autenticado = true;
    ultimoPing = millis();
    Serial.println("[AUTH] ESP32CAM IDENTIFICADA");
    return "HELLO OK";
  }
  if (!autenticado) return "ERROR AUTH";
  if (cmd == "PING") {
    ultimoPing = millis();
    return "PONG";
  }
  if (cmd == "LED ON") {
    luz = true;
    digitalWrite(PIN_RELE, RELE_ON);
    Serial.println("[RELE] ON: GPIO HIGH (prueba NC)");
    return "ACK LED ON";
  }
  if (cmd == "LED OFF") {
    luz = false;
    digitalWrite(PIN_RELE, RELE_OFF);
    Serial.println("[RELE] OFF: GPIO LOW (prueba NC)");
    return "ACK LED OFF";
  }
  if (cmd.startsWith("OBJ ")) {
    objeto = cmd.substring(4);
    Serial.println("[OBJ] LCD -> " + objeto);
    actualizarLCD();
    return "ACK OBJ " + objeto;
  }
  if (cmd == "STATUS") return String("STATUS ") + (luz ? "ON" : "OFF");
  return "ERROR CMD";
}

void setup() {
  Serial.begin(115200);
  digitalWrite(PIN_RELE, RELE_OFF);
  pinMode(PIN_RELE, OUTPUT);
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("INICIANDO...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IP_AP, GATEWAY, SUBNET);
  bool apOK = WiFi.softAP(SSID, PASS, 6, false, 1);
  servidor.begin();
  actualizarLCD();
  Serial.println();
  Serial.println("=== ESP8266 SERVIDOR - PRUEBA RELE NC ===");
  Serial.printf("[AP] %s\n", apOK ? "OK" : "ERROR");
  Serial.print("[IP] ");
  Serial.println(WiFi.softAPIP());
  Serial.println("[RELE] Estado inicial: OFF / GPIO LOW");
  Serial.println("[TCP] Esperando ESP32CAM");
}

void loop() {
  if (tcpActivo && !cliente.connected()) cerrarTCP();
  if (!tcpActivo) {
    WiFiClient nuevo = servidor.available();
    if (nuevo) {
      if (nuevo.remoteIP() != IP_GAFAS) {
        Serial.println("[TCP] IP no permitida");
        nuevo.stop();
      } else {
        cliente = nuevo;
        cliente.setNoDelay(true);
        tcpActivo = true;
        autenticado = false;
        ultimoPing = millis();
        Serial.println("[TCP] SOCKET ABIERTO");
      }
    }
  }
  if (tcpActivo && cliente.connected()) {
    while (cliente.available()) {
      char c = cliente.read();
      if (c == '\n') {
        bufferRX.trim();
        if (bufferRX.length()) {
          Serial.println("[RX] " + bufferRX);
          String respuesta = procesar(bufferRX);
          cliente.println(respuesta);
          Serial.println("[TX] " + respuesta);
        }
        bufferRX = "";
      } else if (c != '\r') {
        if (bufferRX.length() < 80) bufferRX += c;
        else bufferRX = "";
      }
    }
    if (millis() - ultimoPing > TIMEOUT_PING) {
      Serial.println("[TCP] TIMEOUT PING");
      cerrarTCP();
    }
  }
  if (millis() - ultimaLCD >= 250) {
    ultimaLCD = millis();
    actualizarLCD();
  }
  delay(2);
}
