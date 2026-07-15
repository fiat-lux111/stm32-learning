#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

void app_main(void){
	uint32_t count = 0;
	char message[64];

	const char *start_message = "STM32 USART1 test start\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);

	while (1)
	{
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		count++;
		snprintf(message, sizeof(message), "LED Toggle: %lu\r\n", count);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
		HAL_Delay(500);
	}
}