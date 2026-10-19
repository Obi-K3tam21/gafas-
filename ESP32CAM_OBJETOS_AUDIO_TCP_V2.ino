
#include <Arduino.h>
#include <WiFi.h>
#include <HardwareSerial.h>

// ========================================
// CONFIGURACION HW-311
// ========================================

#define AUDIO_RX 13
#define AUDIO_TX 14

HardwareSerial audio(2);

// ========================================
// RED PRIVADA - IP FIJA
// ========================================

const char* SSID = "TESIS_GAFAS";
const char* PASS = "Tesis2026";

IPAddress IP_LOCAL(192, 168, 4, 2);
IPAddress IP_SERVIDOR(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS(192, 168, 4, 1);

WiFiClient tcp;

bool tcpActivo = false;
bool autenticado = false;

String bufferTCP = "";
String bufferSerial = "";

unsigned long ultimoPing = 0;
unsigned long ultimoPong = 0;
unsigned long ultimoIntentoTCP = 0;
unsigned long ultimoIntentoWiFi = 0;
unsigned long inicioConexionTCP = 0;

const unsigned long INTERVALO_PING = 1000;
const unsigned long TIMEOUT_PONG = 3000;
const unsigned long REINTENTO_TCP = 500;
const unsigned long REINTENTO_WIFI = 1000;

// ========================================
// ESTADO DE ARRANQUE
// ========================================

// Se reinicia solamente cuando reinicia
// fisicamente la ESP32-CAM.
bool sistemaIniciado = false;

// ========================================
// COLA DE AUDIO
// ========================================

const uint8_t MAX_COLA = 12;

uint8_t colaAudio[MAX_COLA];
uint8_t colaInicio = 0;
uint8_t colaCantidad = 0;

bool audioOcupado = false;
uint8_t pistaActual = 0;

unsigned long inicioPista = 0;

// Respaldo si el HW-311 no informa
// la finalizacion de una pista.
const unsigned long TIMEOUT_AUDIO = 8000;

// Tiempo minimo antes de aceptar 0x3D
const unsigned long MIN_TIEMPO_AUDIO = 250;

// Parser de tramas UART
uint8_t tramaRX[10];
uint8_t indiceTrama = 0;

// ========================================
// ENVIAR COMANDO AL HW-311
// ========================================

void audioComando(uint8_t cmd, uint16_t param) {

  uint8_t trama[10] = {
    0x7E,
    0xFF,
    0x06,
    cmd,
    0x00,
    uint8_t(param >> 8),
    uint8_t(param),
    0,
    0,
    0xEF
  };

  uint16_t suma = 0;

  for (int i = 1; i <= 6; i++) {
    suma += trama[i];
  }

  uint16_t crc = uint16_t(0 - suma);

  trama[7] = crc >> 8;
  trama[8] = crc & 0xFF;

  audio.write(trama, 10);
}

// ========================================
// AGREGAR AUDIO A LA COLA
// ========================================

void encolarAudio(uint8_t pista) {

  if (pista < 1 || pista > 9) return;

  if (colaCantidad >= MAX_COLA) {
    Serial.println("[AUDIO] Cola llena");
    return;
  }

  uint8_t posicion =
    (colaInicio + colaCantidad) % MAX_COLA;

  colaAudio[posicion] = pista;
  colaCantidad++;

  Serial.printf(
    "[AUDIO] En cola: %d\n",
    pista
  );
}

// ========================================
// FINALIZAR PISTA
// ========================================

void finalizarPista() {

  if (!audioOcupado) return;

  Serial.printf(
    "[AUDIO] Finalizada pista %d\n",
    pistaActual
  );

  audioOcupado = false;
  pistaActual = 0;
}

// ========================================
// RECIBIR TRAMAS HW-311
// ========================================

void procesarTramaAudio() {

  if (tramaRX[0] != 0x7E ||
      tramaRX[9] != 0xEF) {
    return;
  }

  uint8_t comando = tramaRX[3];

  uint16_t parametro =
    (uint16_t(tramaRX[5]) << 8) |
    tramaRX[6];

  // 0x3D = reproduccion finalizada
  if (comando == 0x3D) {

    Serial.printf(
      "[HW-311] Fin de pista, indice %u\n",
      parametro
    );

    if (audioOcupado &&
        millis() - inicioPista >=
        MIN_TIEMPO_AUDIO) {

      finalizarPista();
    }
  }
  else if (comando == 0x3F) {
    Serial.println(
      "[HW-311] Tarjeta SD detectada"
    );
  }
  else if (comando == 0x40) {
    Serial.printf(
      "[HW-311] Error: %u\n",
      parametro
    );
  }
}

// ========================================
// LEER UART AUDIO
// ========================================

void recibirAudio() {

  while (audio.available()) {

    uint8_t dato = audio.read();

    // Esperar inicio de trama
    if (indiceTrama == 0) {
      if (dato != 0x7E) continue;
    }

    tramaRX[indiceTrama++] = dato;

    if (indiceTrama == 10) {

      procesarTramaAudio();

      indiceTrama = 0;
    }
  }
}

// ========================================
// GESTIONAR COLA DE AUDIO
// ========================================

void gestionarAudio() {

  // Comprobar tiempo maximo
  if (audioOcupado &&
      millis() - inicioPista >
      TIMEOUT_AUDIO) {

    Serial.printf(
      "[AUDIO] Timeout pista %d\n",
      pistaActual
    );

    finalizarPista();
  }

  // Si hay audio sonando, esperar
  if (audioOcupado) return;

  // Si no hay audios pendientes
  if (colaCantidad == 0) return;

  // Sacar siguiente pista
  uint8_t siguiente =
    colaAudio[colaInicio];

  colaInicio =
    (colaInicio + 1) % MAX_COLA;

  colaCantidad--;

  // Limpiar respuestas UART antiguas
  while (audio.available()) {
    audio.read();
  }

  indiceTrama = 0;

  pistaActual = siguiente;
  audioOcupado = true;
  inicioPista = millis();

  // Reproduccion por indice
  audioComando(0x03, siguiente);

  Serial.printf(
    "[AUDIO] Reproduciendo pista %d\n",
    siguiente
  );
}

// ========================================
// DESCONECTAR TCP
// ========================================

void cerrarTCP() {

  bool estabaAutenticado = autenticado;

  tcp.stop();

  tcpActivo = false;
  autenticado = false;
  bufferTCP = "";

  Serial.println("[TCP] DESCONECTADO");

  if (estabaAutenticado) {
    encolarAudio(7);
  }
}

// ========================================
// CONECTAR TCP
// ========================================

void conectarTCP() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (tcpActivo) return;

  if (millis() - ultimoIntentoTCP <
      REINTENTO_TCP) {
    return;
  }

  ultimoIntentoTCP = millis();

  Serial.println("[TCP] Conectando...");

  if (tcp.connect(IP_SERVIDOR, 80, 400)) {

    tcp.setNoDelay(true);

    tcpActivo = true;
    autenticado = false;

    inicioConexionTCP = millis();
    ultimoPong = millis();
    ultimoPing = millis();

    tcp.println("HELLO ESP32CAM");

    Serial.println("[TCP] SOCKET ABIERTO");
  }
}

// ========================================
// ENVIAR COMANDOS TCP
// ========================================

void enviarTCP(String comando) {

  if (!tcpActivo ||
      !autenticado ||
      !tcp.connected()) {

    Serial.println(
      "[ERROR] TCP no disponible"
    );

    return;
  }

  tcp.println(comando);

  Serial.println("[TX] " + comando);
}

// ========================================
// PROCESAR RESPUESTAS TCP
// ========================================

void procesarRespuesta(String respuesta) {

  respuesta.trim();

  Serial.println("[RX] " + respuesta);

  if (respuesta == "HELLO OK") {

    autenticado = true;

    ultimoPong = millis();
    ultimoPing = millis();

    Serial.println("[CNT] OK");

    // Primero anunciar conexion
    encolarAudio(6);

    // Sistema iniciado solamente una vez
    if (!sistemaIniciado) {

      encolarAudio(1);

      sistemaIniciado = true;

      Serial.println(
        "[SISTEMA] Primer arranque confirmado"
      );
    }
  }

  else if (respuesta == "PONG") {
    ultimoPong = millis();
  }

  else if (respuesta == "ACK LED ON") {
    encolarAudio(8);
  }

  else if (respuesta == "ACK LED OFF") {
    encolarAudio(9);
  }

  else if (respuesta == "ACK OBJ PERSONA") {
    Serial.println("[LCD] ESP8266 confirmo OBJ: PERSONA");
    encolarAudio(2);
  }

  else if (respuesta == "ACK OBJ TAZA") {
    Serial.println("[LCD] ESP8266 confirmo OBJ: TAZA");
    encolarAudio(3);
  }

  else if (respuesta == "ACK OBJ SILLA") {
    Serial.println("[LCD] ESP8266 confirmo OBJ: SILLA");
    encolarAudio(4);
  }

  else if (respuesta == "ACK OBJ BOTELLA") {
    Serial.println("[LCD] ESP8266 confirmo OBJ: BOTELLA");
    encolarAudio(5);
  }
}

// ========================================
// RECIBIR TCP
// ========================================

void recibirTCP() {

  while (tcp.available()) {

    char c = tcp.read();

    if (c == '\n') {

      bufferTCP.trim();

      if (bufferTCP.length()) {
        procesarRespuesta(bufferTCP);
      }

      bufferTCP = "";
    }

    else if (c != '\r') {

      if (bufferTCP.length() < 80) {
        bufferTCP += c;
      } else {
        bufferTCP = "";
      }
    }
  }
}

// ========================================
// COMANDOS SERIAL
// ========================================

void ejecutar(String cmd) {

  Serial.println("[SERIAL] Comando recibido: " + cmd);

  cmd.trim();
  cmd.toUpperCase();

  // 1, 6 y 7: audio de diagnostico solamente.
  // 2 a 5: objeto por TCP; el ACK activa el audio una sola vez.
  // 8 y 9: control del rele por TCP; el ACK activa el audio.
  if (cmd == "1" || cmd == "6" || cmd == "7") {
    encolarAudio(cmd.toInt());
  }
  else if (cmd == "2") {
    enviarTCP("OBJ PERSONA");
  }
  else if (cmd == "3") {
    enviarTCP("OBJ TAZA");
  }
  else if (cmd == "4") {
    enviarTCP("OBJ SILLA");
  }
  else if (cmd == "5") {
    enviarTCP("OBJ BOTELLA");
  }
  else if (cmd == "8") {
    enviarTCP("LED ON");
  }
  else if (cmd == "9") {
    enviarTCP("LED OFF");
  }

  else if (cmd == "ON") {
    enviarTCP("LED ON");
  }

  else if (cmd == "OFF") {
    enviarTCP("LED OFF");
  }

  else if (cmd == "PING") {
    enviarTCP("PING");
  }

  else if (cmd == "STATUS") {
    enviarTCP("STATUS");
  }

  else if (
    cmd == "PERSONA" ||
    cmd == "TAZA" ||
    cmd == "SILLA" ||
    cmd == "BOTELLA"
  ) {

    enviarTCP("OBJ " + cmd);
  }

  else {
    Serial.println(
      "[ERROR] Comando desconocido"
    );
  }
}

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);

  audio.begin(
    9600,
    SERIAL_8N1,
    AUDIO_RX,
    AUDIO_TX
  );

  delay(1000);

  // Seleccionar tarjeta SD
  audioComando(0x09, 2);

  delay(1000);

  // Volumen
  audioComando(0x06, 25);

  WiFi.mode(WIFI_STA);

  WiFi.config(
    IP_LOCAL,
    IP_SERVIDOR,
    SUBNET,
    DNS
  );

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.begin(SSID, PASS);

  Serial.println();
  Serial.println("======================");
  Serial.println("   GAFAS INTELIGENTES - OBJETOS TCP V2 ");
  Serial.println("======================");

  Serial.println(
    "[WIFI] IP: 192.168.4.2"
  );

  Serial.println(
    "[TCP] Conexion persistente"
  );

  Serial.println(
    "[AUDIO] Cola no bloqueante"
  );

  Serial.println(
    "[SISTEMA] Esperando conexion TCP"
  );

  Serial.println(
    "Comandos: ON, OFF, PING, STATUS,"
    " PERSONA, TAZA, SILLA, BOTELLA, 1-9"
  );
}

// ========================================
// LOOP PRINCIPAL
// ========================================

void loop() {

  // Mantener audio funcionando
  recibirAudio();
  gestionarAudio();

  // =====================================
  // WIFI
  // =====================================

  if (WiFi.status() != WL_CONNECTED) {

    if (tcpActivo) {
      cerrarTCP();
    }

    if (millis() - ultimoIntentoWiFi >=
        REINTENTO_WIFI) {

      ultimoIntentoWiFi = millis();

      WiFi.reconnect();
    }
  }

  // =====================================
  // TCP
  // =====================================

  else {

    if (tcpActivo && !tcp.connected()) {
      cerrarTCP();
    }

    if (!tcpActivo) {
      conectarTCP();
    }

    if (tcpActivo) {

      recibirTCP();

      // Timeout de autenticacion
      if (!autenticado &&
          millis() - inicioConexionTCP > 3000) {

        Serial.println("[AUTH] TIMEOUT");
        cerrarTCP();
      }

      // Timeout de PONG
      if (autenticado &&
          millis() - ultimoPong >
          TIMEOUT_PONG) {

        Serial.println("[TCP] TIMEOUT PONG");
        cerrarTCP();
      }

      // Enviar PING cada segundo
      if (autenticado &&
          tcpActivo &&
          millis() - ultimoPing >=
          INTERVALO_PING) {

        ultimoPing = millis();

        enviarTCP("PING");
      }
    }
  }

  // =====================================
  // MONITOR SERIAL
  // =====================================

  while (Serial.available()) {

    char c = Serial.read();

    if (c == '\n' || c == '\r') {

      if (bufferSerial.length()) {

        ejecutar(bufferSerial);

        bufferSerial = "";
      }
    }

    else {

      if (bufferSerial.length() < 80) {
        bufferSerial += c;
      }
    }
  }

  delay(2);
}
