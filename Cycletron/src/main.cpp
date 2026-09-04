#include <stdio.h>

extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "driver/gpio.h"
}

#include "esp_log.h"
#include "GPIO.h"
#include "DRV8825.h"

static const char *TAG = "GPIO_TEST";

/*
====================================================
FEATURE FLAGS
====================================================
*/
// #define TEST_HELLO_WORLD
// #define TEST_IO_PINS
// #define TEST_MOSFETS
// #define TEST_MOTOR1
// #define TEST_MOTOR2
// #define TEST_SENSORS
#define TEST_DRV8825_MOTOR1
// #define TEST_DRV8825_MOTOR2
#define TEST_WIFI

// Serves a page with FORWARD / STOP / BACKWARD buttons for motor 1.
// Requires TEST_WIFI + TEST_DRV8825_MOTOR1 both defined above.
#define MOTOR1_WEB_CONTROL

#if defined(MOTOR1_WEB_CONTROL) && !(defined(TEST_WIFI) && defined(TEST_DRV8825_MOTOR1))
#error "MOTOR1_WEB_CONTROL requires both TEST_WIFI and TEST_DRV8825_MOTOR1 to be defined"
#endif

#ifdef TEST_WIFI
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/event_groups.h"
#endif

/*
====================================================
PIN MAP STRUCT
====================================================
*/
typedef struct {
    const char *name;
    gpio_num_t pin;
} PinMap;

/*
====================================================
IO PINS (spare, unlabeled on schematic)
====================================================
*/
#ifdef TEST_IO_PINS
static const PinMap io_pins[] = {
    {"IO1",  GPIO_NUM_1},
    {"IO2",  GPIO_NUM_2},
    {"IO37", GPIO_NUM_37},
    {"IO38", GPIO_NUM_38},
    {"IO39", GPIO_NUM_39},
    {"IO40", GPIO_NUM_40},
};
#endif

/*
====================================================
MOSFETS
====================================================
*/
#ifdef TEST_MOSFETS
static const PinMap mosfet_pins[] = {
    {"MOSFET1_I", GPIO_NUM_12}
};
#endif

/*
====================================================
MOTOR 1 (Pololu DRV8825 breakout)
====================================================
*/
#ifdef TEST_MOTOR1
static const PinMap motor1_pins[] = {
    {"M1_EN",   GPIO_NUM_8},
    {"M1_M0",   GPIO_NUM_3},
    // NOTE: M1_M1 is wired to GPIO46, which is INPUT-ONLY on ESP32-S3.
    // It cannot be driven as an output here - left out of this test group.
    {"M1_M2",   GPIO_NUM_9},
    {"M1_STEP", GPIO_NUM_10},
    {"M1_DIR",  GPIO_NUM_11}
};
#endif

#ifdef TEST_MOTOR2
static const PinMap motor2_pins[] = {
    {"M2_EN",   GPIO_NUM_5},
    {"M2_M0",   GPIO_NUM_6},
    {"M2_M1",   GPIO_NUM_7},
    {"M2_M2",   GPIO_NUM_15},
    {"M2_STEP", GPIO_NUM_16},
    {"M2_DIR",  GPIO_NUM_17}
};
#endif

/*
====================================================
SENSORS (HALL)
====================================================
*/
#ifdef TEST_SENSORS
static const PinMap sensor_pins[] = {
    {"HALL_I", GPIO_NUM_45}
};
#endif

/*
====================================================
WIFI / RF TEST
Connects as a station, logs the AP RSSI (pure RF
check), then POSTs the chip's internal temperature
reading to a local server to confirm the full network
stack + a real sensor-to-server round trip.
====================================================
*/
#ifdef TEST_WIFI

#include "driver/temperature_sensor.h"
#include "secrets.h" // WIFI_SSID, WIFI_PASS, WIFI_TEST_URL — see secrets.h.example

#define WIFI_MAX_RETRY  5

static temperature_sensor_handle_t s_temp_handle = NULL;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_wifi_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_wifi_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ESP_LOGI(TAG, "HTTP DATA (%d bytes): %.*s",
                 evt->data_len, evt->data_len, (char *)evt->data);
    }
    return ESP_OK;
}

// One-time WiFi station bring-up. Returns true once connected (or false on
// failure/timeout). Safe to call once at boot.
static bool wifi_test_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi: connecting to SSID: %s", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi RF check OK - RSSI: %d dBm, channel: %d",
                     ap_info.rssi, ap_info.primary);
        }
        return true;
    }

    ESP_LOGE(TAG, "WiFi: failed to connect to SSID: %s", WIFI_SSID);
    return false;
}

// Brings up the ESP32-S3's internal on-die temperature sensor. Low
// precision (it's meant for chip thermal compensation, not lab-grade
// readings), but it's real hardware data to push over the network.
static void wifi_test_temp_sensor_init(void)
{
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &s_temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(s_temp_handle));
}

// Reads the chip temperature and POSTs it as JSON - the "send real sensor
// data to a website and read it back" check.
static void wifi_test_send_temperature(void)
{
    float celsius = 0;
    esp_err_t terr = temperature_sensor_get_celsius(s_temp_handle, &celsius);

    if (terr != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read internal temp sensor: %s", esp_err_to_name(terr));
        return;
    }

    ESP_LOGI(TAG, "Internal chip temp: %.2f C", celsius);

    char post_body[96];
    int len = snprintf(post_body, sizeof(post_body),
                        "{\"device\":\"cycletron_v67\",\"chip_temp_c\":%.2f}", celsius);

    esp_http_client_config_t config = {};
    config.url = WIFI_TEST_URL;
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_body, len);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

#endif // TEST_WIFI

/*
====================================================
DRV8825 MOTOR 1
====================================================
*/
#ifdef TEST_DRV8825_MOTOR1

// WARNING: M1's mode1 pin is wired to GPIO46, which is INPUT-ONLY on the
// ESP32-S3 and cannot be driven by the MCU. Set_Step_Mode() will silently
// fail to change that bit until the schematic is revised to move M1_M1
// off GPIO46. Left mapped here for completeness only.
static DRV8825_t motor1 = {
    .step_pin   = GPIO_NUM_10,
    .dir_pin    = GPIO_NUM_11,
    .fault_pin  = GPIO_NUM_18,
    .mode0_pin  = GPIO_NUM_3,
    .mode1_pin  = GPIO_NUM_46,
    .mode2_pin  = GPIO_NUM_9,
    .enable_pin = GPIO_NUM_8
};

#endif

/*
====================================================
DRV8825 MOTOR 2
====================================================
*/
#ifdef TEST_DRV8825_MOTOR2

static DRV8825_t motor2 = {
    .step_pin   = GPIO_NUM_16,
    .dir_pin    = GPIO_NUM_17,
    .fault_pin  = GPIO_NUM_4,
    .mode0_pin  = GPIO_NUM_6,
    .mode1_pin  = GPIO_NUM_7,
    .mode2_pin  = GPIO_NUM_15,
    .enable_pin = GPIO_NUM_5
};

#endif

/*
====================================================
MOTOR 1 WEB CONTROL
Serves a small page with FORWARD / STOP / BACKWARD
buttons. A dedicated task steps the motor continuously
while a direction is selected, checking the requested
state every ~50 steps so button presses stay responsive.
====================================================
*/
#if defined(TEST_WIFI) && defined(TEST_DRV8825_MOTOR1) && defined(MOTOR1_WEB_CONTROL)

#include <atomic>
#include "esp_http_server.h"

#define MOTOR1_WEB_STEP_DELAY_US 1500 // time between steps -> controls speed
#define MOTOR1_WEB_STEP_BATCH    50   // steps taken before re-checking button state

enum Motor1State {
    MOTOR1_STOP     = 0,
    MOTOR1_FORWARD  = 1,
    MOTOR1_BACKWARD = 2,
};

static std::atomic<int> s_motor1_state{MOTOR1_STOP};

static void motor1_control_task(void *arg)
{
    int last_state = MOTOR1_STOP;
    bool driver_enabled = false;

    while (1) {
        int state = s_motor1_state.load();

        if (state == MOTOR1_STOP) {
            if (driver_enabled) {
                DRV8825_Disable(&motor1);
                driver_enabled = false;
                ESP_LOGI(TAG, "Motor 1: stopped");
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!driver_enabled) {
            DRV8825_Enable(&motor1);
            driver_enabled = true;
        }

        if (state != last_state) {
            DRV8825_Set_Direction(&motor1, state == MOTOR1_FORWARD ? DRV8825_FORWARD : DRV8825_BACKWARD);
            ESP_LOGI(TAG, "Motor 1: %s", state == MOTOR1_FORWARD ? "FORWARD" : "BACKWARD");
        }
        last_state = state;

        for (int i = 0; i < MOTOR1_WEB_STEP_BATCH && s_motor1_state.load() == state; i++) {
            DRV8825_Step(&motor1);
            esp_rom_delay_us(MOTOR1_WEB_STEP_DELAY_US);
        }

        // Yield so the idle task runs (task watchdog) and the HTTP server
        // gets CPU time to service button presses while the motor spins.
        vTaskDelay(1);
    }
}

static esp_err_t motor1_forward_handler(httpd_req_t *req)
{
    s_motor1_state.store(MOTOR1_FORWARD);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "forward");
    return ESP_OK;
}

static esp_err_t motor1_backward_handler(httpd_req_t *req)
{
    s_motor1_state.store(MOTOR1_BACKWARD);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "backward");
    return ESP_OK;
}

static esp_err_t motor1_stop_handler(httpd_req_t *req)
{
    s_motor1_state.store(MOTOR1_STOP);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "stop");
    return ESP_OK;
}

static esp_err_t motor1_status_handler(httpd_req_t *req)
{
    int state = s_motor1_state.load();
    const char *txt = state == MOTOR1_FORWARD ? "forward" : state == MOTOR1_BACKWARD ? "backward" : "stop";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, txt);
    return ESP_OK;
}

static const char MOTOR1_PAGE[] = R"HTML(<!doctype html>
<html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cycletron - Motor 1</title>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; text-align:center; padding:2rem; }
  button { font-size:1.5rem; padding:1.2rem 2rem; margin:0.5rem; border:none; border-radius:12px; width:80%; max-width:320px; }
  #fwd  { background:#2e7d32; color:#fff; }
  #bwd  { background:#1565c0; color:#fff; }
  #stop { background:#c62828; color:#fff; }
  #status { margin-top:1.5rem; font-size:1.2rem; }
</style>
</head><body>
  <h2>Motor 1 Control</h2>
  <div><button id="fwd" onclick="cmd('forward')">FORWARD</button></div>
  <div><button id="bwd" onclick="cmd('backward')">BACKWARD</button></div>
  <div><button id="stop" onclick="cmd('stop')">STOP</button></div>
  <div id="status">status: ...</div>
  <script>
    function cmd(c) { fetch('/motor/' + c).then(poll); }
    function poll() {
      fetch('/motor/status').then(r => r.text()).then(t => {
        document.getElementById('status').innerText = 'status: ' + t;
      });
    }
    setInterval(poll, 1000);
    poll();
  </script>
</body></html>
)HTML";

static esp_err_t motor1_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, MOTOR1_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_motor1_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start motor 1 web server");
        return NULL;
    }

    static const httpd_uri_t root_uri   = { .uri = "/",               .method = HTTP_GET, .handler = motor1_page_handler };
    static const httpd_uri_t fwd_uri    = { .uri = "/motor/forward",  .method = HTTP_GET, .handler = motor1_forward_handler };
    static const httpd_uri_t bwd_uri    = { .uri = "/motor/backward", .method = HTTP_GET, .handler = motor1_backward_handler };
    static const httpd_uri_t stop_uri   = { .uri = "/motor/stop",     .method = HTTP_GET, .handler = motor1_stop_handler };
    static const httpd_uri_t status_uri = { .uri = "/motor/status",   .method = HTTP_GET, .handler = motor1_status_handler };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &fwd_uri);
    httpd_register_uri_handler(server, &bwd_uri);
    httpd_register_uri_handler(server, &stop_uri);
    httpd_register_uri_handler(server, &status_uri);

    return server;
}

#endif // MOTOR1_WEB_CONTROL

/*
====================================================
HELPER
====================================================
*/
static void clear_all(const PinMap *arr, int size)
{
    for (int i = 0; i < size; i++) {
        GPIOHandler::set(arr[i].pin, 0);
    }
}

// Configures each pin in the group as an output, driven low.
static void init_pin_group_outputs(const PinMap *pins, int size)
{
    for (int i = 0; i < size; i++) {
        GPIOHandler::initOutput(pins[i].pin);
        GPIOHandler::set(pins[i].pin, 0);
    }
}

// Walks the group, driving one pin high at a time (all others low),
// logging + holding for delay_ms before moving to the next.
static void cycle_pin_group(const char *label, const PinMap *pins, int size, int delay_ms)
{
    for (int i = 0; i < size; i++) {
        clear_all(pins, size);
        GPIOHandler::set(pins[i].pin, 1);
        ESP_LOGI(TAG, "%s ACTIVE: %s (%d)", label, pins[i].name, pins[i].pin);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// Runs one forward/backward move cycle on a DRV8825 motor, logging each
// phase. Used by the plain (non-web-control) DRV8825 tests.
static void test_drv8825_cycle(DRV8825_t *motor, const char *label)
{
    ESP_LOGI(TAG, "%s FORWARD", label);
    DRV8825_Move(motor, 200, DRV8825_FORWARD, 10000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "%s Backwards", label);
    DRV8825_Move(motor, 200, DRV8825_BACKWARD, 10000);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/*
====================================================
MAIN
====================================================
*/
extern "C" void app_main(void)
{
    /*
    ====================================================
    HELLO WORLD / BOOT CHECK
    Unconditional, runs regardless of feature flags above.
    If you see this counting down in the serial monitor,
    the flash succeeded and the chip is not crash-looping.
    ====================================================
    */
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  Cycletron hello world - firmware booted and running");
    ESP_LOGI(TAG, "==================================================");

    for (int i = 3; i > 0; i--) {
        ESP_LOGI(TAG, "Boot check heartbeat... %d", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "GPIO modular test starting...");

    /*
    ====================================================
    INIT ALL ACTIVE GROUPS
    ====================================================
    */

#ifdef TEST_IO_PINS
    init_pin_group_outputs(io_pins, sizeof(io_pins) / sizeof(io_pins[0]));
#endif

#ifdef TEST_MOSFETS
    init_pin_group_outputs(mosfet_pins, sizeof(mosfet_pins) / sizeof(mosfet_pins[0]));
#endif

#ifdef TEST_MOTOR1
    init_pin_group_outputs(motor1_pins, sizeof(motor1_pins) / sizeof(motor1_pins[0]));
#endif

#ifdef TEST_MOTOR2
    init_pin_group_outputs(motor2_pins, sizeof(motor2_pins) / sizeof(motor2_pins[0]));
#endif

#ifdef TEST_SENSORS
    int s_size = sizeof(sensor_pins) / sizeof(sensor_pins[0]);

    for (int i = 0; i < s_size; i++) {

        gpio_config_t cfg = {};

        cfg.pin_bit_mask = (1ULL << sensor_pins[i].pin);
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;

        gpio_config(&cfg);
    }
#endif

#ifdef TEST_DRV8825_MOTOR1

    ESP_LOGI(TAG, "Initializing DRV8825 Motor 1");

    DRV8825_Init(&motor1);

#if !defined(MOTOR1_WEB_CONTROL)
    DRV8825_Enable(&motor1);
#endif

    DRV8825_Set_Step_Mode(
        &motor1,
        DRV8825_FULL_STEP
    );

#endif

#ifdef TEST_DRV8825_MOTOR2

    ESP_LOGI(TAG, "Initializing DRV8825 Motor 2");

    DRV8825_Init(&motor2);

    DRV8825_Enable(&motor2);

    DRV8825_Set_Step_Mode(
        &motor2,
        DRV8825_FULL_STEP
    );

#endif

#ifdef TEST_WIFI

    bool wifi_ok = wifi_test_connect();

#ifndef MOTOR1_WEB_CONTROL
    if (wifi_ok) {
        wifi_test_temp_sensor_init();
        wifi_test_send_temperature();
    }
#endif

#endif

#if defined(TEST_WIFI) && defined(TEST_DRV8825_MOTOR1) && defined(MOTOR1_WEB_CONTROL)

    if (wifi_ok) {
        start_motor1_webserver();
        xTaskCreate(motor1_control_task, "motor1_ctrl", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "Motor 1 web control ready - open http://<device-ip>/ in a browser");
    } else {
        ESP_LOGE(TAG, "Motor 1 web control skipped - WiFi not connected");
    }

#endif

    /*
    ====================================================
    MAIN TEST LOOP
    ====================================================
    */
    while (1) {

#ifdef TEST_IO_PINS
        cycle_pin_group("IO", io_pins, sizeof(io_pins) / sizeof(io_pins[0]), 5000);
#endif

#ifdef TEST_MOSFETS
        cycle_pin_group("MOSFET", mosfet_pins, sizeof(mosfet_pins) / sizeof(mosfet_pins[0]), 5000);
#endif

#ifdef TEST_MOTOR1
        cycle_pin_group("MOTOR1", motor1_pins, sizeof(motor1_pins) / sizeof(motor1_pins[0]), 5000);
#endif

#ifdef TEST_MOTOR2
        cycle_pin_group("MOTOR2", motor2_pins, sizeof(motor2_pins) / sizeof(motor2_pins[0]), 3000);
#endif

#ifdef TEST_SENSORS

        for (int i = 0; i < s_size; i++) {

            int val = gpio_get_level(sensor_pins[i].pin);

            ESP_LOGI(TAG,
                     "SENSOR READ: %s (%d) = %d",
                     sensor_pins[i].name,
                     sensor_pins[i].pin,
                     val);

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

#endif

#if defined(TEST_DRV8825_MOTOR1) && !defined(MOTOR1_WEB_CONTROL)
        test_drv8825_cycle(&motor1, "Motor 1");
#endif

#ifdef TEST_DRV8825_MOTOR2
        test_drv8825_cycle(&motor2, "Motor 2");
#endif

#ifdef TEST_WIFI
#ifdef MOTOR1_WEB_CONTROL

        // Nothing to do here - the HTTP server and motor1_control_task
        // handle everything. Just yield so this task doesn't starve the
        // idle task and trip the watchdog.
        vTaskDelay(pdMS_TO_TICKS(1000));

#else

        if (wifi_ok) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                ESP_LOGI(TAG, "WIFI RSSI: %d dBm", ap_info.rssi);
            }
            wifi_test_send_temperature();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));

#endif // MOTOR1_WEB_CONTROL
#endif // TEST_WIFI

#ifdef TEST_HELLO_WORLD

        // Keeps logging so you can see the board is alive and not
        // crash-looping, and yields to the idle task so the watchdog
        // doesn't fire.
        ESP_LOGI(TAG, "Hello world still alive...");
        vTaskDelay(pdMS_TO_TICKS(2000));

#endif

    }
}
