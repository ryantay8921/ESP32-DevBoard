#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include "driver/gpio.h"

class GPIOHandler {
public:
    // Initialize a pin as output
    static void initOutput(gpio_num_t pin);

    // Initialize a pin as input
    static void initInput(gpio_num_t pin, bool pullup = true, bool pulldown = false);

    // Set output level
    static void set(gpio_num_t pin, int level);

    // Toggle output
    static void toggle(gpio_num_t pin);

    // Read input level
    static int read(gpio_num_t pin);
};

#endif