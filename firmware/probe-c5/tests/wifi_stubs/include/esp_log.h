#pragma once

#include <stdio.h>

#define ESP_LOGI(tag, format, ...) \
    do { \
        (void)(tag); \
        if (0) \
            (void)fprintf(stderr, format, ##__VA_ARGS__); \
    } while (0)
#define ESP_LOGW(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
