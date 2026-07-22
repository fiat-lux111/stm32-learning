#ifndef OLED_H
#define OLED_H

#include "main.h"
#include <stdint.h>

#define OLED_WIDTH 128U
#define OLED_HEIGHT 64U

void oled_init(I2C_HandleTypeDef *hi2c);
void oled_clear(void);
void oled_update(void);
void oled_set_cursor(uint8_t x, uint8_t page);
void oled_write_string(const char *str);

#endif
