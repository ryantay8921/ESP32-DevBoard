#include <stdio.h>

extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "driver/gpio.h"
}

#include "esp_log.h"
#include "GPIO.h"

static const char *TAG = "GPIO_TEST";

/*
====================================================
FEATURE FLAGS
====================================================
*/
#define TEST_IO_PINS
// #define TEST_MOSFETS
// #define TEST_MOTOR1
// #define TEST_SENSORS

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
IO PINS
====================================================
*/
#ifdef TEST_IO_PINS
static const PinMap io_pins[] = {
    {"IO4",  GPIO_NUM_4},
    {"IO5",  GPIO_NUM_5},
    {"IO6",  GPIO_NUM_6},
    {"IO7",  GPIO_NUM_7},
    {"IO8",  GPIO_NUM_15},
    {"IO9",  GPIO_NUM_16},
    {"IO10", GPIO_NUM_17},
    {"IO11", GPIO_NUM_18},
    {"IO12", GPIO_NUM_8},
    {"IO15", GPIO_NUM_3},
    {"IO16", GPIO_NUM_46},
    {"IO17", GPIO_NUM_9},
    {"IO18", GPIO_NUM_10},
    {"IO19", GPIO_NUM_11},
    {"IO20", GPIO_NUM_12},
};
#endif

/*
====================================================
MOSFETS
====================================================
*/
#ifdef TEST_MOSFETS
static const PinMap mosfet_pins[] = {
    {"MOSFET_1", GPIO_NUM_14},
    {"MOSFET_2", GPIO_NUM_21},
    {"MOSFET_3", GPIO_NUM_47}
};
#endif

/*
====================================================
MOTOR 1
====================================================
*/
#ifdef TEST_MOTOR1
static const PinMap motor1_pins[] = {
    {"M1_FLT",  GPIO_NUM_48},
    {"M1_EN",   GPIO_NUM_45},
    {"M1_M0",   GPIO_NUM_35},
    {"M1_M1",   GPIO_NUM_36},
    {"M1_M2",   GPIO_NUM_37},
    {"M1_STEP", GPIO_NUM_38},
    {"M1_DIR",  GPIO_NUM_39}
};
#endif

/*
====================================================
SENSORS (THERMISTOR)
====================================================
*/
#ifdef TEST_SENSORS
static const PinMap sensor_pins[] = {
    {"THERMISTOR_MINUS", GPIO_NUM_21}
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
    ESP_LOGI(TAG, "GPIO modular test starting...");

    while (1) {

        /*
        =====================
        INIT ALL ACTIVE GROUPS
        =====================
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

        /*
        =====================
        TEST LOOP
        =====================
        */

#ifdef TEST_IO_PINS
        for (int i = 0; i < io_size; i++) {

            clear_all(io_pins, io_size);
            GPIOHandler::set(io_pins[i].pin, 1);

            ESP_LOGI(TAG, "IO ACTIVE: %s (%d)",
                     io_pins[i].name,
                     io_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }
#endif

#ifdef TEST_MOSFETS
        for (int i = 0; i < mos_size; i++) {

            clear_all(mosfet_pins, mos_size);
            GPIOHandler::set(mosfet_pins[i].pin, 1);

            ESP_LOGI(TAG, "MOSFET ACTIVE: %s (%d)",
                     mosfet_pins[i].name,
                     mosfet_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }
#endif

#ifdef TEST_MOTOR1
        for (int i = 0; i < m1_size; i++) {

            clear_all(motor1_pins, m1_size);
            GPIOHandler::set(motor1_pins[i].pin, 1);

            ESP_LOGI(TAG, "MOTOR1 ACTIVE: %s (%d)",
                     motor1_pins[i].name,
                     motor1_pins[i].pin);

            vTaskDelay(pdMS_TO_TICKS(5000));
        }
#endif

#ifdef TEST_SENSORS
        for (int i = 0; i < s_size; i++) {

            int val = gpio_get_level(sensor_pins[i].pin);

            ESP_LOGI(TAG, "SENSOR READ: %s (%d) = %d",
                     sensor_pins[i].name,
                     sensor_pins[i].pin,
                     val);

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
#endif

    }
}