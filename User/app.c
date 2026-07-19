#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;

static volatile uint8_t key_pressed_flag = 0;
static volatile uint8_t tim2_tick_flag = 0;
static volatile uint8_t uart_rx_flag = 0;
static uint8_t uart_rx_data = 0;

#define LIGHT_DARK_THRESHOLD 2500U

static uint16_t app_read_adc(void)
{
	uint16_t adc_value = 0;

	HAL_ADC_Start(&hadc1);
	if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
	{
		adc_value = (uint16_t)HAL_ADC_GetValue(&hadc1);
	}
	HAL_ADC_Stop(&hadc1);

	return adc_value;
}

void app_main(void){
	uint32_t key_count = 0;
	uint32_t tim2_count = 0;
	uint32_t last_adc_time = 0;
	uint32_t last_alarm_blink_time = 0;
	uint8_t light_alarm_active = 0;
	char message[64];
	HAL_UART_Transmit(&huart1, (uint8_t*)"NEW FIRMWARE\r\n", 15, HAL_MAX_DELAY);

	const char *start_message = "ADC alarm test start\r\nCommand: 1=toggle, ?=help, a=read adc\r\n";
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
				const char *reply = "Command: 1=toggle, ?=help, a=read adc\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 'a' || uart_rx_data == 'A')
			{
				uint16_t adc_value = app_read_adc();
				uint32_t voltage_mv = adc_value * 3300UL / 4095UL;
				snprintf(message, sizeof(message), "ADC Raw: %u, Voltage: %lu.%03luV\r\n", adc_value, voltage_mv / 1000, voltage_mv % 1000);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
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

		if (HAL_GetTick() - last_adc_time >= 1000)
		{
			last_adc_time = HAL_GetTick();
			uint16_t adc_value = app_read_adc();
			uint32_t voltage_mv = adc_value * 3300UL / 4095UL;
			uint8_t is_dark = (adc_value > LIGHT_DARK_THRESHOLD);

			snprintf(message, sizeof(message), "ADC Raw: %u, Voltage: %lu.%03luV\r\n", adc_value, voltage_mv / 1000, voltage_mv % 1000);
			HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);

			if (is_dark && !light_alarm_active)
			{
				light_alarm_active = 1;
				const char *alarm_message = "Light Low! Alarm ON\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)alarm_message, strlen(alarm_message), HAL_MAX_DELAY);
			}
			else if (!is_dark && light_alarm_active)
			{
				light_alarm_active = 0;
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
				const char *alarm_message = "Light OK. Alarm OFF\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)alarm_message, strlen(alarm_message), HAL_MAX_DELAY);
			}
		}

		if (light_alarm_active && HAL_GetTick() - last_alarm_blink_time >= 300)
		{
			last_alarm_blink_time = HAL_GetTick();
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
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