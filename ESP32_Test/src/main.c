#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

void app_main(void)
{
    while (1) {
        printf("Hello world\n");
        fflush(stdout);
        ESP_LOGI("MAIN", "Hello world");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}