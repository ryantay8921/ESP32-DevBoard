#include "DRV8825.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   // for esp_rom_delay_us

static const char *TAG = "DRV8825";

// Internal helper to init a pin
static void gpio_init_output(gpio_num_t pin) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

static void gpio_init_input(gpio_num_t pin) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void DRV8825_Init(DRV8825_t *motor)
{
    gpio_init_output(motor->step_pin);
    gpio_init_output(motor->dir_pin);
    gpio_init_output(motor->enable_pin);
    gpio_init_output(motor->mode0_pin);
    gpio_init_output(motor->mode1_pin);
    gpio_init_output(motor->mode2_pin);

    gpio_init_input(motor->fault_pin);

    gpio_set_level(motor->step_pin, 0);
    gpio_set_level(motor->dir_pin, DRV8825_FORWARD);

    DRV8825_Disable(motor);

    if (DRV8825_Check_Fault(motor)) {
        ESP_LOGE(TAG, "Driver fault detected during init");
    }
}

bool DRV8825_Check_Fault(DRV8825_t *motor)
{
    return gpio_get_level(motor->fault_pin) == 0; // active LOW
}

void DRV8825_Enable(DRV8825_t *motor)
{
    gpio_set_level(motor->enable_pin, 0); // active LOW
}

void DRV8825_Disable(DRV8825_t *motor)
{
    gpio_set_level(motor->enable_pin, 1);
}

void DRV8825_Set_Direction(DRV8825_t *motor, int direction)
{
    if (direction == DRV8825_FORWARD) {
        gpio_set_level(motor->dir_pin, 1);
    } else if (direction == DRV8825_BACKWARD) {
        gpio_set_level(motor->dir_pin, 0);
    } else {
        ESP_LOGE(TAG, "Invalid direction");
    }
}

void DRV8825_Step(DRV8825_t *motor)
{
    gpio_set_level(motor->step_pin, 1);
    esp_rom_delay_us(2);

    gpio_set_level(motor->step_pin, 0);
    esp_rom_delay_us(2);
}

void DRV8825_Step_N(DRV8825_t *motor, int steps, int delay_us)
{
    DRV8825_Enable(motor);

    for (int i = 0; i < steps; i++) {
        DRV8825_Step(motor);
        esp_rom_delay_us(delay_us);
    }
}

void DRV8825_Move(DRV8825_t *motor, int steps, int direction, int delay_us)
{
    DRV8825_Set_Direction(motor, direction);
    DRV8825_Step_N(motor, steps, delay_us);
}

void DRV8825_Set_Step_Mode(DRV8825_t *motor, int mode)
{
    gpio_set_level(motor->mode0_pin,  mode & 0x01);
    gpio_set_level(motor->mode1_pin, (mode >> 1) & 0x01);
    gpio_set_level(motor->mode2_pin, (mode >> 2) & 0x01);
}