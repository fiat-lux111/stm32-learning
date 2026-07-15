#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;

static volatile uint8_t key_pressed_flag = 0;
static volatile uint8_t tim2_tick_flag = 0;
static volatile uint8_t uart_rx_flag = 0;
static uint8_t uart_rx_data = 0;

void app_main(void){
	uint32_t key_count = 0;
	uint32_t tim2_count = 0;
	char message[64];
	HAL_UART_Transmit(&huart1, (uint8_t*)"NEW FIRMWARE\r\n", 15, HAL_MAX_DELAY);

	const char *start_message = "UART command test start\r\nCommand: 1=toggle, ?=help\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);
	//HAL_TIM_Base_Start_IT(&htim2);
	HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);

	while (1)
	{
		if (uart_rx_flag)
		{
			uart_rx_flag = 0;

			if (uart_rx_data == '1')
			{
				HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
				const char *reply = "LED Toggle\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == '?')
			{
				const char *reply = "Command: 1=toggle, ?=help\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
			}
			else if (uart_rx_data != '\r' && uart_rx_data != '\n')
			{
				snprintf(message, sizeof(message), "Unknown command: %c\r\n", uart_rx_data);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
		}

		if (key_pressed_flag)
		{
			key_pressed_flag = 0;
			HAL_UART_Transmit(&huart1, (uint8_t*)"KEY triggered\r\n", 15, HAL_MAX_DELAY);
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
			//HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART1)
	{
		uart_rx_flag = 1;
		HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);
	}
}