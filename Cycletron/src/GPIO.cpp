#include "GPIO.h"

void GPIOHandler::initOutput(gpio_num_t pin)
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << pin);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    gpio_config(&config);
}

void GPIOHandler::initInput(gpio_num_t pin, bool pullup, bool pulldown)
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << pin);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    gpio_config(&config);
}

void GPIOHandler::set(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);
}

void GPIOHandler::toggle(gpio_num_t pin)
{
    int current = gpio_get_level(pin);
    gpio_set_level(pin, !current);
}

int GPIOHandler::read(gpio_num_t pin)
{
    return gpio_get_level(pin);
}