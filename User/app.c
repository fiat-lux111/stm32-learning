#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;

static volatile uint8_t key_pressed_flag = 0;
static volatile uint8_t tim2_tick_flag = 0;

void app_main(void){
	uint32_t key_count = 0;
	uint32_t tim2_count = 0;
	char message[64];

	const char *start_message = "PA1 EXTI and TIM2 test start\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);
	HAL_TIM_Base_Start_IT(&htim2);

	while (1)
	{
		if (key_pressed_flag)
		{
			key_pressed_flag = 0;
			HAL_Delay(20);
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET)
			{
				HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
				key_count++;
				snprintf(message, sizeof(message), "EXTI Key Pressed: %lu\r\n", key_count);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
		}

		if (tim2_tick_flag)
		{
			tim2_tick_flag = 0;
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			tim2_count++;
			snprintf(message, sizeof(message), "TIM2 Tick: %lu\r\n", tim2_count);
			HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
		}

		HAL_Delay(100);
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == GPIO_PIN_1)
	{
		key_pressed_flag = 1;
	}
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM2)
	{
		tim2_tick_flag = 1;
	}
}