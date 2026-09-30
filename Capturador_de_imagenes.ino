#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"

// ======================================================
// WIFI
// ======================================================

const char* ssid = ".TigoWiFi-429459600/0_EXT";
const char* password = "WiFi-96821666";

// ======================================================
// ESP32-CAM AI THINKER
// ======================================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

httpd_handle_t webServer = NULL;
httpd_handle_t streamServer = NULL;

// ======================================================
// HTML + CSS + JS
// ======================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width, initial-scale=1.0">

<title>ESP32-CAM Dataset Collector</title>

<style>

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: Arial, Helvetica, sans-serif;
  background: #0c1420;
  color: white;
}

.container {
  max-width: 1100px;
  margin: 20px auto;
  padding: 20px;
}

.card {
  background: #101d2d;
  border: 1px solid #24364d;
  border-radius: 12px;
  padding: 20px;
  box-shadow: 0 4px 18px rgba(0,0,0,0.35);
}

h1 {
  margin-top: 0;
}

.info {
  color: #aebdd1;
}

.camera {
  width: 100%;
  max-width: 700px;
  display: block;
  background: black;
  border-radius: 10px;
  border: 1px solid #304762;
  margin-bottom: 20px;
}

.controls {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  align-items: center;
}

input {
  background: #0c1725;
  border: 1px solid #334b68;
  color: white;
  padding: 11px;
  border-radius: 7px;
  font-size: 15px;
}

button {
  border: none;
  padding: 11px 16px;
  border-radius: 7px;
  cursor: pointer;
  color: white;
  font-weight: bold;
  font-size: 14px;
}

button:hover {
  opacity: 0.88;
}

.start {
  background: #10a95f;
}

.capture {
  background: #1976ff;
}

.clear {
  background: #dd3545;
}

.download-selected {
  background: #7b4dff;
}

.download-all {
  background: #2387c8;
}

.status {
  margin-top: 18px;
  padding: 12px;
  background: #0c1725;
  border: 1px solid #283b54;
  border-radius: 8px;
}

.gallery-title {
  margin-top: 25px;
}

.gallery {
  display: flex;
  overflow-x: auto;
  gap: 10px;
  padding: 10px 0;
  min-height: 130px;
}

.thumb {
  min-width: 150px;
  width: 150px;
  background: #0d1725;
  border: 2px solid transparent;
  border-radius: 8px;
  cursor: pointer;
  position: relative;
}

.thumb.selected {
  border-color: #1986ff;
}

.thumb img {
  width: 100%;
  height: 110px;
  object-fit: cover;
  border-radius: 6px 6px 0 0;
}

.thumb-name {
  padding: 7px;
  font-size: 11px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.badge {
  position: absolute;
  top: 6px;
  right: 6px;
  background: #1986ff;
  color: white;
  width: 23px;
  height: 23px;
  border-radius: 50%;
  display: none;
  align-items: center;
  justify-content: center;
  font-weight: bold;
}

.thumb.selected .badge {
  display: flex;
}

@media(max-width:700px) {

  .controls {
    flex-direction: column;
    align-items: stretch;
  }

  button,
  input {
    width: 100%;
  }
}

</style>

</head>

<body>

<div class="container">

<div class="card">

<h1>ESP32-CAM Dataset Collector</h1>

<p class="info">
Captura imágenes para entrenamiento de Edge Impulse.
</p>

<img
id="stream"
class="camera">

<div class="controls">

<input
type="text"
id="filename"
value="background"
placeholder="Nombre base">

<input
type="number"
id="interval"
value="1000"
min="300"
step="100">

<span>ms</span>

<button
id="startButton"
class="start"
onclick="toggleCollection()">

▶ Iniciar captura

</button>

<button
class="capture"
onclick="captureImage()">

📷 Capturar

</button>

<button
class="clear"
onclick="clearImages()">

🗑 Limpiar

</button>

<button
class="download-selected"
onclick="downloadSelectedImages()">

⬇ Descargar seleccionadas

</button>

<button
class="download-all"
onclick="downloadAllImages()">

⬇ Descargar todas

</button>

</div>

<div class="status">

Estado:
<strong id="status">Listo</strong>

&nbsp; | &nbsp;

Capturas:
<strong id="counter">0</strong>

&nbsp; | &nbsp;

Seleccionadas:
<strong id="selectedCounter">0</strong>

</div>

<h3 class="gallery-title">
Imágenes capturadas
</h3>

<div
id="gallery"
class="gallery">
</div>

</div>

</div>

<script>

// ======================================================
// VARIABLES
// ======================================================

let images = [];

let collecting = false;

let collectionTimer = null;

let imageNumber = 1;

// ======================================================
// STREAM
// ======================================================

window.onload = function() {

  const host =
    window.location.hostname;

  document.getElementById(
    "stream"
  ).src =
    "http://" +
    host +
    ":81/stream";

};

// ======================================================
// NOMBRE DEL ARCHIVO
// ======================================================

function generateFilename() {

  let base =
    document.getElementById(
      "filename"
    ).value.trim();

  if (!base) {

    base =
      "imagen";

  }

  const now =
    new Date();

  const date =
    now.getFullYear().toString() +

    String(
      now.getMonth() + 1
    ).padStart(2,"0") +

    String(
      now.getDate()
    ).padStart(2,"0");

  const time =

    String(
      now.getHours()
    ).padStart(2,"0") +

    String(
      now.getMinutes()
    ).padStart(2,"0") +

    String(
      now.getSeconds()
    ).padStart(2,"0");

  const number =
    String(
      imageNumber
    ).padStart(
      3,
      "0"
    );

  return (
    base +
    "_" +
    date +
    "_" +
    time +
    "_" +
    number +
    ".jpg"
  );
}

// ======================================================
// CAPTURA
// ======================================================

async function captureImage() {

  try {

    document.getElementById(
      "status"
    ).innerText =
      "Capturando...";

    const response =
      await fetch(
        "/capture?t=" +
        Date.now()
      );

    if (!response.ok) {

      throw new Error(
        "Error HTTP"
      );

    }

    const blob =
      await response.blob();

    const filename =
      generateFilename();

    const url =
      URL.createObjectURL(
        blob
      );

    images.push({

      filename:
        filename,

      blob:
        blob,

      url:
        url,

      selected:
        false

    });

    imageNumber++;

    addThumbnail(
      images.length - 1
    );

    updateCounters();

    document.getElementById(
      "status"
    ).innerText =
      "Captura OK";

  }

  catch(error) {

    console.error(error);

    document.getElementById(
      "status"
    ).innerText =
      "Error capturando";

  }
}

// ======================================================
// MINIATURA
// ======================================================

function addThumbnail(index) {

  const image =
    images[index];

  const div =
    document.createElement(
      "div"
    );

  div.className =
    "thumb";

  div.innerHTML =

    "<div class='badge'>✓</div>" +

    "<img src='" +
    image.url +
    "'>" +

    "<div class='thumb-name'>" +
    image.filename +
    "</div>";

  div.onclick =
    function() {

      image.selected =
        !image.selected;

      div.classList.toggle(
        "selected",
        image.selected
      );

      updateCounters();

    };

  document.getElementById(
    "gallery"
  ).appendChild(
    div
  );
}

// ======================================================
// CONTADORES
// ======================================================

function updateCounters() {

  document.getElementById(
    "counter"
  ).innerText =
    images.length;

  const selected =
    images.filter(
      img =>
        img.selected
    ).length;

  document.getElementById(
    "selectedCounter"
  ).innerText =
    selected;
}

// ======================================================
// CAPTURA AUTOMÁTICA
// ======================================================

function toggleCollection() {

  const button =
    document.getElementById(
      "startButton"
    );

  if (!collecting) {

    let interval =
      parseInt(
        document.getElementById(
          "interval"
        ).value
      );

    if (
      isNaN(interval) ||
      interval < 300
    ) {

      interval =
        1000;

    }

    collecting =
      true;

    button.innerText =
      "⏹ Detener captura";

    document.getElementById(
      "status"
    ).innerText =
      "Captura automática";

    captureImage();

    collectionTimer =
      setInterval(
        captureImage,
        interval
      );

  }

  else {

    collecting =
      false;

    clearInterval(
      collectionTimer
    );

    button.innerText =
      "▶ Iniciar captura";

    document.getElementById(
      "status"
    ).innerText =
      "Captura detenida";

  }
}

// ======================================================
// LIMPIAR TODO
// ======================================================

function clearImages() {

  if (
    images.length === 0
  ) {

    return;

  }

  if (
    !confirm(
      "¿Eliminar todas las capturas?"
    )
  ) {

    return;

  }

  images.forEach(

    img =>
      URL.revokeObjectURL(
        img.url
      )

  );

  images = [];

  imageNumber = 1;

  document.getElementById(
    "gallery"
  ).innerHTML =
    "";

  updateCounters();

  document.getElementById(
    "status"
  ).innerText =
    "Galería vacía";
}

// ======================================================
// CRC32 PARA ZIP
// ======================================================

const crcTable = (() => {

  let table = [];

  for (
    let n = 0;
    n < 256;
    n++
  ) {

    let c = n;

    for (
      let k = 0;
      k < 8;
      k++
    ) {

      c =
        ((c & 1)
          ? (0xEDB88320 ^ (c >>> 1))
          : (c >>> 1));

    }

    table[n] =
      c >>> 0;

  }

  return table;

})();

function crc32(bytes) {

  let crc =
    0 ^ (-1);

  for (
    let i = 0;
    i < bytes.length;
    i++
  ) {

    crc =
      (crc >>> 8) ^

      crcTable[
        (crc ^ bytes[i]) &
        0xFF
      ];

  }

  return (
    crc ^ (-1)
  ) >>> 0;
}

// ======================================================
// CREAR ZIP
// ======================================================

async function createZip(files) {

  const encoder =
    new TextEncoder();

  let fileParts = [];

  let centralParts = [];

  let offset = 0;

  for (
    const file of files
  ) {

    const data =
      new Uint8Array(
        await file.blob.arrayBuffer()
      );

    const name =
      encoder.encode(
        file.filename
      );

    const crc =
      crc32(data);

    // =====================================
    // LOCAL HEADER
    // =====================================

    const local =
      new Uint8Array(
        30 +
        name.length
      );

    const lv =
      new DataView(
        local.buffer
      );

    lv.setUint32(
      0,
      0x04034b50,
      true
    );

    lv.setUint16(
      4,
      20,
      true
    );

    lv.setUint16(
      6,
      0,
      true
    );

    lv.setUint16(
      8,
      0,
      true
    );

    lv.setUint16(
      10,
      0,
      true
    );

    lv.setUint16(
      12,
      0,
      true
    );

    lv.setUint32(
      14,
      crc,
      true
    );

    lv.setUint32(
      18,
      data.length,
      true
    );

    lv.setUint32(
      22,
      data.length,
      true
    );

    lv.setUint16(
      26,
      name.length,
      true
    );

    lv.setUint16(
      28,
      0,
      true
    );

    local.set(
      name,
      30
    );

    fileParts.push(
      local
    );

    fileParts.push(
      data
    );

    // =====================================
    // CENTRAL DIRECTORY
    // =====================================

    const central =
      new Uint8Array(
        46 +
        name.length
      );

    const cv =
      new DataView(
        central.buffer
      );

    cv.setUint32(
      0,
      0x02014b50,
      true
    );

    cv.setUint16(
      4,
      20,
      true
    );

    cv.setUint16(
      6,
      20,
      true
    );

    cv.setUint16(
      8,
      0,
      true
    );

    cv.setUint16(
      10,
      0,
      true
    );

    cv.setUint16(
      12,
      0,
      true
    );

    cv.setUint16(
      14,
      0,
      true
    );

    cv.setUint32(
      16,
      crc,
      true
    );

    cv.setUint32(
      20,
      data.length,
      true
    );

    cv.setUint32(
      24,
      data.length,
      true
    );

    cv.setUint16(
      28,
      name.length,
      true
    );

    cv.setUint16(
      30,
      0,
      true
    );

    cv.setUint16(
      32,
      0,
      true
    );

    cv.setUint16(
      34,
      0,
      true
    );

    cv.setUint16(
      36,
      0,
      true
    );

    cv.setUint32(
      38,
      0,
      true
    );

    cv.setUint32(
      42,
      offset,
      true
    );

    central.set(
      name,
      46
    );

    centralParts.push(
      central
    );

    offset +=
      local.length +
      data.length;
  }

  const centralOffset =
    offset;

  let centralSize =
    0;

  for (
    const part of centralParts
  ) {

    centralSize +=
      part.length;

  }

  // =====================================
  // END CENTRAL DIRECTORY
  // =====================================

  const end =
    new Uint8Array(22);

  const ev =
    new DataView(
      end.buffer
    );

  ev.setUint32(
    0,
    0x06054b50,
    true
  );

  ev.setUint16(
    4,
    0,
    true
  );

  ev.setUint16(
    6,
    0,
    true
  );

  ev.setUint16(
    8,
    files.length,
    true
  );

  ev.setUint16(
    10,
    files.length,
    true
  );

  ev.setUint32(
    12,
    centralSize,
    true
  );

  ev.setUint32(
    16,
    centralOffset,
    true
  );

  ev.setUint16(
    20,
    0,
    true
  );

  return new Blob(

    [
      ...fileParts,
      ...centralParts,
      end
    ],

    {
      type:
        "application/zip"
    }

  );
}

// ======================================================
// GUARDAR ZIP
// ======================================================

function saveZip(
  blob,
  filename
) {

  const url =
    URL.createObjectURL(
      blob
    );

  const link =
    document.createElement(
      "a"
    );

  link.href =
    url;

  link.download =
    filename;

  document.body.appendChild(
    link
  );

  link.click();

  document.body.removeChild(
    link
  );

  setTimeout(
    () => {

      URL.revokeObjectURL(
        url
      );

    },
    1500
  );
}

// ======================================================
// DESCARGAR SELECCIONADAS
// ======================================================

async function downloadSelectedImages() {

  const selected =
    images.filter(
      img =>
        img.selected
    );

  if (
    selected.length === 0
  ) {

    alert(
      "Selecciona al menos una imagen."
    );

    return;
  }

  document.getElementById(
    "status"
  ).innerText =
    "Creando ZIP...";

  const zipBlob =
    await createZip(
      selected
    );

  let base =
    document.getElementById(
      "filename"
    ).value.trim();

  if (!base) {

    base =
      "dataset";

  }

  const zipName =

    base +
    "_seleccion_" +
    selected.length +
    ".zip";

  saveZip(
    zipBlob,
    zipName
  );

  document.getElementById(
    "status"
  ).innerText =
    "ZIP descargado";
}

// ======================================================
// DESCARGAR TODAS
// ======================================================

async function downloadAllImages() {

  if (
    images.length === 0
  ) {

    alert(
      "No hay imágenes."
    );

    return;
  }

  document.getElementById(
    "status"
  ).innerText =
    "Creando ZIP...";

  const zipBlob =
    await createZip(
      images
    );

  let base =
    document.getElementById(
      "filename"
    ).value.trim();

  if (!base) {

    base =
      "dataset";

  }

  const zipName =

    base +
    "_todas_" +
    images.length +
    ".zip";

  saveZip(
    zipBlob,
    zipName
  );

  document.getElementById(
    "status"
  ).innerText =
    "ZIP descargado";
}

</script>

</body>
</html>

)rawliteral";

// ======================================================
// INDEX
// ======================================================

static esp_err_t index_handler(
  httpd_req_t *req
) {

  httpd_resp_set_type(
    req,
    "text/html"
  );

  return httpd_resp_send(
    req,
    INDEX_HTML,
    HTTPD_RESP_USE_STRLEN
  );
}

// ======================================================
// CAPTURA JPEG
// ======================================================

static esp_err_t capture_handler(
  httpd_req_t *req
) {

  camera_fb_t *fb =
    esp_camera_fb_get();

  if (!fb) {

    httpd_resp_send_500(
      req
    );

    return ESP_FAIL;
  }

  httpd_resp_set_type(
    req,
    "image/jpeg"
  );

  httpd_resp_set_hdr(
    req,
    "Cache-Control",
    "no-store"
  );

  esp_err_t result =
    httpd_resp_send(

      req,

      (const char *)
      fb->buf,

      fb->len
    );

  esp_camera_fb_return(
    fb
  );

  return result;
}

// ======================================================
// STREAM
// ======================================================

static esp_err_t stream_handler(
  httpd_req_t *req
) {

  camera_fb_t *fb =
    NULL;

  esp_err_t res =
    ESP_OK;

  char buffer[128];

  res =
    httpd_resp_set_type(

      req,

      "multipart/x-mixed-replace;"
      "boundary=frame"
    );

  if (
    res != ESP_OK
  ) {

    return res;
  }

  while (true) {

    fb =
      esp_camera_fb_get();

    if (!fb) {

      res =
        ESP_FAIL;

      break;
    }

    size_t headerLength =

      snprintf(

        buffer,

        sizeof(buffer),

        "--frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n",

        fb->len
      );

    res =
      httpd_resp_send_chunk(

        req,

        buffer,

        headerLength
      );

    if (
      res == ESP_OK
    ) {

      res =
        httpd_resp_send_chunk(

          req,

          (const char *)
          fb->buf,

          fb->len
        );
    }

    if (
      res == ESP_OK
    ) {

      res =
        httpd_resp_send_chunk(

          req,

          "\r\n",

          2
        );
    }

    esp_camera_fb_return(
      fb
    );

    fb =
      NULL;

    if (
      res != ESP_OK
    ) {

      break;
    }

    delay(
      40
    );
  }

  return res;
}

// ======================================================
// WEB SERVER
// ======================================================

void startWebServer() {

  httpd_config_t config =
    HTTPD_DEFAULT_CONFIG();

  config.server_port =
    80;

  httpd_uri_t index_uri = {

    .uri = "/",

    .method = HTTP_GET,

    .handler = index_handler,

    .user_ctx = NULL
  };

  httpd_uri_t capture_uri = {

    .uri = "/capture",

    .method = HTTP_GET,

    .handler = capture_handler,

    .user_ctx = NULL
  };

  if (
    httpd_start(
      &webServer,
      &config
    )
    ==
    ESP_OK
  ) {

    httpd_register_uri_handler(
      webServer,
      &index_uri
    );

    httpd_register_uri_handler(
      webServer,
      &capture_uri
    );
  }

  // ====================================================
  // STREAM PUERTO 81
  // ====================================================

  httpd_config_t streamConfig =
    HTTPD_DEFAULT_CONFIG();

  streamConfig.server_port =
    81;

  streamConfig.ctrl_port =
    config.ctrl_port + 1;

  httpd_uri_t stream_uri = {

    .uri = "/stream",

    .method = HTTP_GET,

    .handler = stream_handler,

    .user_ctx = NULL
  };

  if (
    httpd_start(
      &streamServer,
      &streamConfig
    )
    ==
    ESP_OK
  ) {

    httpd_register_uri_handler(
      streamServer,
      &stream_uri
    );
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(
    1500
  );

  Serial.println();

  Serial.println(
    "ESP32-CAM Dataset Collector"
  );

  // ====================================================
  // CAMERA CONFIG
  // ====================================================

  camera_config_t config;

  config.ledc_channel =
    LEDC_CHANNEL_0;

  config.ledc_timer =
    LEDC_TIMER_0;

  config.pin_d0 =
    Y2_GPIO_NUM;

  config.pin_d1 =
    Y3_GPIO_NUM;

  config.pin_d2 =
    Y4_GPIO_NUM;

  config.pin_d3 =
    Y5_GPIO_NUM;

  config.pin_d4 =
    Y6_GPIO_NUM;

  config.pin_d5 =
    Y7_GPIO_NUM;

  config.pin_d6 =
    Y8_GPIO_NUM;

  config.pin_d7 =
    Y9_GPIO_NUM;

  config.pin_xclk =
    XCLK_GPIO_NUM;

  config.pin_pclk =
    PCLK_GPIO_NUM;

  config.pin_vsync =
    VSYNC_GPIO_NUM;

  config.pin_href =
    HREF_GPIO_NUM;

  config.pin_sccb_sda =
    SIOD_GPIO_NUM;

  config.pin_sccb_scl =
    SIOC_GPIO_NUM;

  config.pin_pwdn =
    PWDN_GPIO_NUM;

  config.pin_reset =
    RESET_GPIO_NUM;

  config.xclk_freq_hz =
    20000000;

  config.pixel_format =
    PIXFORMAT_JPEG;

  config.frame_size =
    FRAMESIZE_QVGA;

  config.jpeg_quality =
    10;

  config.fb_count =
    2;

  config.grab_mode =
    CAMERA_GRAB_LATEST;

  config.fb_location =
    CAMERA_FB_IN_PSRAM;

  // ====================================================
  // INICIAR CÁMARA
  // ====================================================

  esp_err_t error =
    esp_camera_init(
      &config
    );

  if (
    error != ESP_OK
  ) {

    Serial.printf(

      "Error cámara: 0x%x\n",

      error
    );

    return;
  }

  Serial.println(
    "Cámara OK"
  );

  // ====================================================
  // SENSOR
  // ====================================================

  sensor_t *sensor =
    esp_camera_sensor_get();

  if (
    sensor
  ) {

    sensor->set_brightness(
      sensor,
      1
    );

    sensor->set_contrast(
      sensor,
      1
    );

    sensor->set_saturation(
      sensor,
      0
    );

    sensor->set_whitebal(
      sensor,
      1
    );

    sensor->set_awb_gain(
      sensor,
      1
    );

    sensor->set_exposure_ctrl(
      sensor,
      1
    );

    sensor->set_gain_ctrl(
      sensor,
      1
    );
  }

  // ====================================================
  // WIFI
  // ====================================================

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    ssid,
    password
  );

  Serial.print(
    "Conectando WiFi"
  );

  while (
    WiFi.status()
    !=
    WL_CONNECTED
  ) {

    delay(
      500
    );

    Serial.print(
      "."
    );
  }

  Serial.println();

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  // ====================================================
  // mDNS
  // ====================================================

  if (
    MDNS.begin(
      "esp32cam"
    )
  ) {

    Serial.println(
      "http://esp32cam.local"
    );
  }

  // ====================================================
  // WEB
  // ====================================================

  startWebServer();

  Serial.println();

  Serial.println(
    "Servidor listo"
  );

  Serial.print(
    "Abrir: http://"
  );

  Serial.println(
    WiFi.localIP()
  );
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  delay(
    1000
  );
}