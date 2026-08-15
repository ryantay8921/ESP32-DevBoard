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
// #define TEST_DRV8825_MOTOR1
// #define TEST_DRV8825_MOTOR2
#define TEST_WIFI

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
HELPER
====================================================
*/
static void clear_all(const PinMap *arr, int size)
{
    for (int i = 0; i < size; i++) {
        GPIOHandler::set(arr[i].pin, 0);
    }
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
    int io_size = sizeof(io_pins) / sizeof(io_pins[0]);

    for (int i = 0; i < io_size; i++) {
        GPIOHandler::initOutput(io_pins[i].pin);
        GPIOHandler::set(io_pins[i].pin, 0);
    }
#endif

#ifdef TEST_MOSFETS
    int mos_size = sizeof(mosfet_pins) / sizeof(mosfet_pins[0]);

    for (int i = 0; i < mos_size; i++) {
        GPIOHandler::initOutput(mosfet_pins[i].pin);
        GPIOHandler::set(mosfet_pins[i].pin, 0);
    }
#endif

#ifdef TEST_MOTOR1
    int m1_size = sizeof(motor1_pins) / sizeof(motor1_pins[0]);

    for (int i = 0; i < m1_size; i++) {
        GPIOHandler::initOutput(motor1_pins[i].pin);
        GPIOHandler::set(motor1_pins[i].pin, 0);
    }
#endif

#ifdef TEST_MOTOR2
    int m2_size = sizeof(motor2_pins) / sizeof(motor2_pins[0]);

    for (int i = 0; i < m2_size; i++) {
        GPIOHandler::initOutput(motor2_pins[i].pin);
        GPIOHandler::set(motor2_pins[i].pin, 0);
    }
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

    DRV8825_Enable(&motor1);

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

    if (wifi_ok) {
        wifi_test_temp_sensor_init();
        wifi_test_send_temperature();
    }

#endif

    /*
    ====================================================
    MAIN TEST LOOP
    ====================================================
    */
    while (1) {

#ifdef TEST_IO_PINS

        for (int i = 0; i < io_size; i++) {

            clear_all(io_pins, io_size);

            GPIOHandler::set(io_pins[i].pin, 1);

            ESP_LOGI(TAG,
                     "IO ACTIVE: %s (%d)",
                     io_pins[i].name,
                     io_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }

#endif

#ifdef TEST_MOSFETS

        for (int i = 0; i < mos_size; i++) {

            clear_all(mosfet_pins, mos_size);

            GPIOHandler::set(mosfet_pins[i].pin, 1);

            ESP_LOGI(TAG,
                     "MOSFET ACTIVE: %s (%d)",
                     mosfet_pins[i].name,
                     mosfet_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }

#endif

#ifdef TEST_MOTOR1

        for (int i = 0; i < m1_size; i++) {

            clear_all(motor1_pins, m1_size);

            GPIOHandler::set(motor1_pins[i].pin, 1);

            ESP_LOGI(TAG,
                     "MOTOR1 ACTIVE: %s (%d)",
                     motor1_pins[i].name,
                     motor1_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }

#endif

#ifdef TEST_MOTOR2

        for (int i = 0; i < m2_size; i++) {

            clear_all(motor2_pins, m2_size);

            GPIOHandler::set(motor2_pins[i].pin, 1);

            ESP_LOGI(TAG,
                     "MOTOR2 ACTIVE: %s (%d)",
                     motor2_pins[i].name,
                     motor2_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(3000));
        }

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

#ifdef TEST_DRV8825_MOTOR1

        ESP_LOGI(TAG, "Motor 1 FORWARD");

        DRV8825_Move(
            &motor1,
            200,
            DRV8825_FORWARD,
            10000
        );

        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Motor 1 Backwards");

        DRV8825_Move(
            &motor1,
            200,
            DRV8825_BACKWARD,
            10000
        );

        vTaskDelay(pdMS_TO_TICKS(2000));

#endif

#ifdef TEST_DRV8825_MOTOR2

        ESP_LOGI(TAG, "Motor 2 FORWARD");

        DRV8825_Move(
            &motor2,
            200,
            DRV8825_FORWARD,
            10000
        );

        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Motor 2 Backwards");

        DRV8825_Move(
            &motor2,
            200,
            DRV8825_BACKWARD,
            10000
        );

        vTaskDelay(pdMS_TO_TICKS(2000));

#endif

#ifdef TEST_WIFI

        if (wifi_ok) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                ESP_LOGI(TAG, "WIFI RSSI: %d dBm", ap_info.rssi);
            }
            wifi_test_send_temperature();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));

#endif

#ifdef TEST_HELLO_WORLD

        // Keeps logging so you can see the board is alive and not
        // crash-looping, and yields to the idle task so the watchdog
        // doesn't fire.
        ESP_LOGI(TAG, "Hello world still alive...");
        vTaskDelay(pdMS_TO_TICKS(2000));

#endif

    }
}
