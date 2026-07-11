#ifndef DRV8825_H
#define DRV8825_H

#include "driver/gpio.h"
#include <stdbool.h>

// === Direction Constants ===
#define DRV8825_FORWARD  1
#define DRV8825_BACKWARD 0

// === Default Step Delay (us) ===
#define DRV8825_DEFAULT_STEP_DELAY_US 1000

// === Microstepping Modes ===
#define DRV8825_FULL_STEP          0
#define DRV8825_HALF_STEP          1
#define DRV8825_QUARTER_STEP       2
#define DRV8825_EIGHTH_STEP        3
#define DRV8825_SIXTEENTH_STEP     4
#define DRV8825_THIRTYSECOND_STEP  7

typedef struct {
    gpio_num_t step_pin;
    gpio_num_t dir_pin;
    gpio_num_t fault_pin;
    gpio_num_t mode0_pin;
    gpio_num_t mode1_pin;
    gpio_num_t mode2_pin;
    gpio_num_t enable_pin;
} DRV8825_t;

// API
void DRV8825_Init(DRV8825_t *motor);
bool DRV8825_Check_Fault(DRV8825_t *motor);
void DRV8825_Enable(DRV8825_t *motor);
void DRV8825_Disable(DRV8825_t *motor);
void DRV8825_Set_Direction(DRV8825_t *motor, int direction);
void DRV8825_Step(DRV8825_t *motor);
void DRV8825_Step_N(DRV8825_t *motor, int steps, int delay_us);
void DRV8825_Move(DRV8825_t *motor, int steps, int direction, int delay_us);
void DRV8825_Set_Step_Mode(DRV8825_t *motor, int mode);

#endif