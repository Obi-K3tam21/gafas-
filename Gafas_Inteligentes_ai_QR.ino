/*
 * Gafas inteligentes - ESP32-CAM AI Thinker
 * Edge Impulse + QR ON/OFF - VERSION 4
 *
 * Arquitectura:
 * - La OV2640 vuelve a la configuracion ORIGINAL ESTABLE:
 *      JPEG + QVGA 320x240 + XCLK 20 MHz + fb_count 1
 * - La camara se inicializa UNA sola vez.
 * - QR e IA nunca llaman esp_camera_fb_get() al mismo tiempo:
 *      un mutex protege la captura.
 * - QR convierte JPEG -> RGB888 -> GRAYSCALE para quirc.
 * - Edge Impulse convierte JPEG -> RGB888 como en el sketch original.
 * - IA corre cada 5 s.
 *
 * Logica QR:
 * - Estado inicial: espera ON.
 * - ON requiere 3 lecturas validas consecutivas.
 * - Tras confirmar ON, los nuevos ON se ignoran y solo se acepta OFF.
 * - OFF requiere 3 lecturas validas consecutivas.
 * - Tras confirmar OFF, los nuevos OFF se ignoran y solo se acepta ON.
 *
 * Aun NO se envia nada al ESP8266.
 */

#include <gafas_inteligente_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"
#include "img_converters.h"

#include <ESP32QRCodeReader.h>
#include "quirc/quirc.h"

// -----------------------------------------------------------------------------
// AI Thinker camera pins
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Edge Impulse frame settings
// -----------------------------------------------------------------------------
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE         3

static bool debug_nn = false;
static bool is_initialised = false;

static uint8_t *snapshot_buf = nullptr;

// -----------------------------------------------------------------------------
// Camera: ORIGINAL stable configuration
// -----------------------------------------------------------------------------
static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// -----------------------------------------------------------------------------
// Shared camera synchronization
// -----------------------------------------------------------------------------
SemaphoreHandle_t cameraMutex = nullptr;

// -----------------------------------------------------------------------------
// QR
// -----------------------------------------------------------------------------
enum EstadoQR {
    ESPERANDO_ON,
    ESPERANDO_OFF
};

EstadoQR estadoQR = ESPERANDO_ON;

String qrPendiente = "";
uint8_t qrConteo = 0;
const uint8_t QR_CONFIRMACIONES = 3;

// If too much time passes between confirmations, restart the count.
unsigned long ultimaLecturaQRCandidata = 0;
const unsigned long QR_TIMEOUT_CONFIRMACION = 2000;

struct QRMensaje {
    char payload[128];
};

QueueHandle_t qrQueue = nullptr;
TaskHandle_t qrTaskHandle = nullptr;

// RGB work buffer exclusively used by QR task
static uint8_t *qr_rgb_buf = nullptr;

// -----------------------------------------------------------------------------
// IA every 5 seconds
// -----------------------------------------------------------------------------
unsigned long ultimaInferencia = 0;
const unsigned long INTERVALO_IA = 5000;

// -----------------------------------------------------------------------------
// Function declarations
// -----------------------------------------------------------------------------
bool iniciarCamara();
void ejecutarInferencia();

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
);

static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
);

void qrTask(void *pvParameters);
void procesarQR(const String &contenido);

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" GAFAS - EDGE IMPULSE + QR V4");
    Serial.println(" ESP32-CAM AI THINKER");
    Serial.println("========================================");

    if (!psramFound()) {
        Serial.println("ERROR: PSRAM no encontrada.");
        while (true) {
            delay(1000);
        }
    }

    cameraMutex = xSemaphoreCreateMutex();

    if (cameraMutex == nullptr) {
        Serial.println("ERROR: no se pudo crear cameraMutex.");
        while (true) {
            delay(1000);
        }
    }

    qrQueue = xQueueCreate(8, sizeof(QRMensaje));

    if (qrQueue == nullptr) {
        Serial.println("ERROR: no se pudo crear qrQueue.");
        while (true) {
            delay(1000);
        }
    }

    if (!iniciarCamara()) {
        Serial.println("ERROR: no se pudo inicializar la camara.");
        while (true) {
            delay(1000);
        }
    }

    const size_t rgbSize =
        EI_CAMERA_RAW_FRAME_BUFFER_COLS *
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
        EI_CAMERA_FRAME_BYTE_SIZE;

    snapshot_buf = (uint8_t *)ps_malloc(rgbSize);
    qr_rgb_buf = (uint8_t *)ps_malloc(rgbSize);

    if (snapshot_buf == nullptr || qr_rgb_buf == nullptr) {
        Serial.println("ERROR: no se pudieron reservar los buffers RGB.");
        while (true) {
            delay(1000);
        }
    }

    Serial.printf("Buffer IA reservado: %u bytes\n", (unsigned)rgbSize);
    Serial.printf("Buffer QR reservado: %u bytes\n", (unsigned)rgbSize);

    BaseType_t taskOK = xTaskCreatePinnedToCore(
        qrTask,
        "qrTask",
        16 * 1024,
        nullptr,
        1,
        &qrTaskHandle,
        0
    );

    if (taskOK != pdPASS) {
        Serial.println("ERROR: no se pudo crear la tarea QR.");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("QR activo: OK");
    Serial.println("QR: 3 confirmaciones | estado inicial: esperando ON");
    Serial.println("IA: una inferencia cada 5 s");
    Serial.println("Camara: JPEG/QVGA/20MHz + mutex");
    Serial.println();

    ultimaInferencia = millis();
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------
void loop()
{
    QRMensaje mensaje;

    while (xQueueReceive(qrQueue, &mensaje, 0) == pdTRUE) {
        procesarQR(String(mensaje.payload));
    }

    if (millis() - ultimaInferencia >= INTERVALO_IA) {
        ultimaInferencia = millis();
        ejecutarInferencia();
    }

    delay(5);
}

// -----------------------------------------------------------------------------
// Camera init
// -----------------------------------------------------------------------------
bool iniciarCamara()
{
    esp_err_t err = esp_camera_init(&camera_config);

    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();

    if (s == nullptr) {
        Serial.println("ERROR: sensor de camara no disponible.");
        return false;
    }

    is_initialised = true;

    Serial.println("Camara inicializada: OK");
    return true;
}

// -----------------------------------------------------------------------------
// QR task
//
// The QR task gets a JPEG frame, converts it to RGB888 while holding the
// camera mutex, then RETURNS the framebuffer. QR decoding happens after that.
// -----------------------------------------------------------------------------
void qrTask(void *pvParameters)
{
    struct quirc *q = quirc_new();

    if (q == nullptr) {
        Serial.println("QR ERROR: quirc_new fallo.");
        vTaskDelete(nullptr);
        return;
    }

    if (quirc_resize(
            q,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS) < 0) {

        Serial.println("QR ERROR: quirc_resize fallo.");
        quirc_destroy(q);
        vTaskDelete(nullptr);
        return;
    }

    while (true) {

        // ~5-6 QR attempts per second.
        // This leaves plenty of camera windows for the 5-second IA.
        vTaskDelay(pdMS_TO_TICKS(180));

        if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == nullptr) {
            xSemaphoreGive(cameraMutex);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        bool convertido = false;

        if (fb->format == PIXFORMAT_JPEG) {
            convertido = fmt2rgb888(
                fb->buf,
                fb->len,
                PIXFORMAT_JPEG,
                qr_rgb_buf
            );
        }

        // IMPORTANT: return camera buffer before doing quirc processing.
        esp_camera_fb_return(fb);
        fb = nullptr;

        xSemaphoreGive(cameraMutex);

        if (!convertido) {
            continue;
        }

        int qw = 0;
        int qh = 0;

        uint8_t *gray = quirc_begin(q, &qw, &qh);

        if (gray == nullptr) {
            continue;
        }

        const int width = EI_CAMERA_RAW_FRAME_BUFFER_COLS;
        const int height = EI_CAMERA_RAW_FRAME_BUFFER_ROWS;
        const size_t pixels = (size_t)width * height;

        // fmt2rgb888 output is RGB888.
        // Convert to grayscale for quirc.
        for (size_t i = 0; i < pixels; i++) {

            uint8_t r = qr_rgb_buf[(i * 3) + 0];
            uint8_t g = qr_rgb_buf[(i * 3) + 1];
            uint8_t b = qr_rgb_buf[(i * 3) + 2];

            // Fast integer luma approximation
            gray[i] =
                (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
        }

        quirc_end(q);

        int count = quirc_count(q);

        for (int i = 0; i < count; i++) {

            struct quirc_code code;
            struct quirc_data data;

            quirc_extract(q, i, &code);

            quirc_decode_error_t err =
                quirc_decode(&code, &data);

            if (err != QUIRC_SUCCESS) {
                continue;
            }

            if (data.payload_len == 0) {
                continue;
            }

            QRMensaje msg = {};

            size_t len = data.payload_len;

            if (len >= sizeof(msg.payload)) {
                len = sizeof(msg.payload) - 1;
            }

            memcpy(msg.payload, data.payload, len);
            msg.payload[len] = '\0';

            xQueueSend(qrQueue, &msg, 0);
        }
    }
}

// -----------------------------------------------------------------------------
// QR state machine: ON -> OFF -> ON
// -----------------------------------------------------------------------------
void procesarQR(const String &entrada)
{
    String contenido = entrada;

    contenido.trim();
    contenido.toUpperCase();

    if (contenido.length() == 0) {
        return;
    }

    const char *esperado =
        (estadoQR == ESPERANDO_ON) ? "ON" : "OFF";

    // Ignore the QR that does not correspond to the current expected state.
    if (contenido != esperado) {
        return;
    }

    // Confirmation timeout
    if (millis() - ultimaLecturaQRCandidata > QR_TIMEOUT_CONFIRMACION) {
        qrPendiente = "";
        qrConteo = 0;
    }

    ultimaLecturaQRCandidata = millis();

    if (contenido == qrPendiente) {
        qrConteo++;
    }
    else {
        qrPendiente = contenido;
        qrConteo = 1;
    }

    Serial.printf(
        "QR candidato: %s [%u/%u]\n",
        contenido.c_str(),
        qrConteo,
        QR_CONFIRMACIONES
    );

    if (qrConteo < QR_CONFIRMACIONES) {
        return;
    }

    qrConteo = 0;
    qrPendiente = "";

    Serial.println();
    Serial.println("============================");
    Serial.printf("QR CONFIRMADO: %s\n", contenido.c_str());
    Serial.println("============================");

    if (contenido == "ON") {

        estadoQR = ESPERANDO_OFF;

        Serial.println(
            "ON bloqueado. Ahora solo se acepta OFF."
        );

        // FUTURO:
        // enviarAlESP8266("ON");
    }
    else {

        estadoQR = ESPERANDO_ON;

        Serial.println(
            "OFF bloqueado. Ahora solo se acepta ON."
        );

        // FUTURO:
        // enviarAlESP8266("OFF");
    }

    Serial.println();
}

// -----------------------------------------------------------------------------
// Edge Impulse
// -----------------------------------------------------------------------------
void ejecutarInferencia()
{
    ei::signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;

    signal.get_data = &ei_camera_get_data;

    if (!ei_camera_capture(
            (size_t)EI_CLASSIFIER_INPUT_WIDTH,
            (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
            snapshot_buf)) {

        ei_printf("Failed to capture image\r\n");
        return;
    }

    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR err =
        run_classifier(&signal, &result, debug_nn);

    if (err != EI_IMPULSE_OK) {

        ei_printf(
            "ERR: Failed to run classifier (%d)\n",
            err
        );

        return;
    }

    ei_printf(
        "\nPredictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.):\n",
        result.timing.dsp,
        result.timing.classification,
        result.timing.anomaly
    );

#if EI_CLASSIFIER_OBJECT_DETECTION == 1

    ei_printf(
        "ERROR: se esperaba un modelo de clasificacion.\r\n"
    );

#else

    ei_printf("Predictions:\r\n");

    uint16_t best_index = 0;
    float best_score = 0.0f;

    for (uint16_t i = 0;
         i < EI_CLASSIFIER_LABEL_COUNT;
         i++) {

        float score =
            result.classification[i].value;

        ei_printf(
            "  %s: %.5f\r\n",
            ei_classifier_inferencing_categories[i],
            score
        );

        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    const char *best_label =
        ei_classifier_inferencing_categories[best_index];

    // Keep current test threshold.
    const float UMBRAL = 0.40f;

    if (best_score >= UMBRAL) {

        if (strcmp(best_label, "background") == 0) {

            ei_printf(
                "\r\nESCENA LIBRE / BACKGROUND\r\n"
            );

            ei_printf(
                "CONFIANZA: %.2f %%\r\n\r\n",
                best_score * 100.0f
            );
        }
        else {

            ei_printf(
                "\r\n============================\r\n"
            );

            ei_printf(
                "DETECTADO: %s\r\n",
                best_label
            );

            ei_printf(
                "CONFIANZA: %.2f %%\r\n",
                best_score * 100.0f
            );

            ei_printf(
                "============================\r\n\r\n"
            );

            // FUTURO:
            // enviarAlESP8266(best_label);
        }
    }
    else {

        ei_printf(
            "\r\nSIN DETECCION CONFIABLE\r\n"
        );

        ei_printf(
            "Mejor candidato: %s (%.2f %%)\r\n\r\n",
            best_label,
            best_score * 100.0f
        );
    }

#endif

#if EI_CLASSIFIER_HAS_ANOMALY

    ei_printf(
        "Anomaly prediction: %.3f\r\n",
        result.anomaly
    );

#endif
}

// -----------------------------------------------------------------------------
// EI camera capture - original JPEG route, now protected by mutex
// -----------------------------------------------------------------------------
bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf)
{
    if (!is_initialised) {

        ei_printf(
            "ERR: Camera is not initialized\r\n"
        );

        return false;
    }

    if (xSemaphoreTake(
            cameraMutex,
            pdMS_TO_TICKS(1500)) != pdTRUE) {

        ei_printf(
            "ERR: timeout esperando la camara\r\n"
        );

        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == nullptr) {

        xSemaphoreGive(cameraMutex);

        ei_printf(
            "Camera capture failed\n"
        );

        return false;
    }

    bool converted =
        fmt2rgb888(
            fb->buf,
            fb->len,
            PIXFORMAT_JPEG,
            out_buf
        );

    esp_camera_fb_return(fb);
    fb = nullptr;

    xSemaphoreGive(cameraMutex);

    if (!converted) {

        ei_printf(
            "Conversion failed\n"
        );

        return false;
    }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) ||
        (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {

        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height
        );
    }

    return true;
}

// -----------------------------------------------------------------------------
// EI data callback
// -----------------------------------------------------------------------------
static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr)
{
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {

        // Preserve the same channel order behavior as the working EI sketch.
        out_ptr[out_ptr_ix] =
            (snapshot_buf[pixel_ix + 2] << 16) +
            (snapshot_buf[pixel_ix + 1] << 8) +
            snapshot_buf[pixel_ix];

        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }

    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif


