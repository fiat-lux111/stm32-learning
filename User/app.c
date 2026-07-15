#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

void app_main(void){
	uint32_t key_count = 0;
	char message[64];
	GPIO_PinState last_key_state = GPIO_PIN_SET;

	const char *start_message = "PA1 key test start\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);

	while (1)
	{
		GPIO_PinState key_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);

		if (last_key_state == GPIO_PIN_SET && key_state == GPIO_PIN_RESET)
		{
			HAL_Delay(20);
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET)
			{
				HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
				key_count++;
				snprintf(message, sizeof(message), "Key Pressed: %lu\r\n", key_count);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
		}

		last_key_state = key_state;
		HAL_Delay(10);
	}
}