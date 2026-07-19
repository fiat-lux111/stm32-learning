#include "app.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern ADC_HandleTypeDef hadc1;

static volatile uint8_t key_up_pressed_flag = 0;
static volatile uint8_t key_down_pressed_flag = 0;
static volatile uint8_t tim2_tick_flag = 0;
static volatile uint8_t uart_rx_flag = 0;
static uint8_t uart_rx_data = 0;

#define LIGHT_THRESHOLD_STEP 100U
#define LIGHT_THRESHOLD_MIN 100U
#define LIGHT_THRESHOLD_MAX 4000U
#define PWM_DUTY_STEP 10U
#define PWM_DUTY_MAX 100U

static uint16_t light_dark_threshold = 2500;
static uint8_t pwm_duty_percent = 50;

static void app_pwm_set_duty(uint8_t duty_percent)
{
	if (duty_percent > PWM_DUTY_MAX)
	{
		duty_percent = PWM_DUTY_MAX;
	}

	uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1UL;
	uint32_t pulse = period * duty_percent / 100UL;

	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse);
	pwm_duty_percent = duty_percent;
}

static void app_pwm_duty_up(void)
{
	if (pwm_duty_percent + PWM_DUTY_STEP <= PWM_DUTY_MAX)
	{
		app_pwm_set_duty(pwm_duty_percent + PWM_DUTY_STEP);
	}
}

static void app_pwm_duty_down(void)
{
	if (pwm_duty_percent >= PWM_DUTY_STEP)
	{
		app_pwm_set_duty(pwm_duty_percent - PWM_DUTY_STEP);
	}
}

static void app_threshold_up(void)
{
	if (light_dark_threshold + LIGHT_THRESHOLD_STEP <= LIGHT_THRESHOLD_MAX)
	{
		light_dark_threshold += LIGHT_THRESHOLD_STEP;
	}
}

static void app_threshold_down(void)
{
	if (light_dark_threshold >= LIGHT_THRESHOLD_MIN + LIGHT_THRESHOLD_STEP)
	{
		light_dark_threshold -= LIGHT_THRESHOLD_STEP;
	}
}

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
	uint32_t key_up_count = 0;
	uint32_t key_down_count = 0;
	uint32_t tim2_count = 0;
	uint32_t last_adc_time = 0;
	uint32_t last_alarm_blink_time = 0;
	uint8_t light_alarm_active = 0;
	char message[64];
	HAL_UART_Transmit(&huart1, (uint8_t*)"NEW FIRMWARE\r\n", 15, HAL_MAX_DELAY);

	const char *start_message = "ADC + PWM test start\r\nCommand: 1=toggle, ?=help, a=read adc, +=threshold up, -=threshold down, t=threshold, u=pwm up, d=pwm down, p=pwm\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	app_pwm_set_duty(pwm_duty_percent);
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
				const char *reply = "Command: 1=toggle, ?=help, a=read adc, +=threshold up, -=threshold down, t=threshold, u=pwm up, d=pwm down, p=pwm\r\n";
				HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 'a' || uart_rx_data == 'A')
			{
				uint16_t adc_value = app_read_adc();
				uint32_t voltage_mv = adc_value * 3300UL / 4095UL;
				snprintf(message, sizeof(message), "ADC Raw: %u, Voltage: %lu.%03luV\r\n", adc_value, voltage_mv / 1000, voltage_mv % 1000);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == '+')
			{
				app_threshold_up();
				snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == '-')
			{
				app_threshold_down();
				snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 't' || uart_rx_data == 'T')
			{
				snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 'u' || uart_rx_data == 'U')
			{
				app_pwm_duty_up();
				snprintf(message, sizeof(message), "PWM Duty: %u%%\r\n", pwm_duty_percent);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 'd' || uart_rx_data == 'D')
			{
				app_pwm_duty_down();
				snprintf(message, sizeof(message), "PWM Duty: %u%%\r\n", pwm_duty_percent);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data == 'p' || uart_rx_data == 'P')
			{
				snprintf(message, sizeof(message), "PWM Duty: %u%%\r\n", pwm_duty_percent);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
			else if (uart_rx_data != '\r' && uart_rx_data != '\n')
			{
				snprintf(message, sizeof(message), "Unknown command: %c\r\n", uart_rx_data);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
		}

		if (key_up_pressed_flag)
		{
			key_up_pressed_flag = 0;
			HAL_Delay(20);
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET)
			{
				key_up_count++;
				app_threshold_up();
				snprintf(message, sizeof(message), "KEY UP: %lu, Threshold: %u\r\n", key_up_count, light_dark_threshold);
				HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			}
		}

		if (key_down_pressed_flag)
		{
			key_down_pressed_flag = 0;
			HAL_Delay(20);
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_RESET)
			{
				key_down_count++;
				app_threshold_down();
				snprintf(message, sizeof(message), "KEY DOWN: %lu, Threshold: %u\r\n", key_down_count, light_dark_threshold);
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
			uint8_t is_dark = (adc_value > light_dark_threshold);

			snprintf(message, sizeof(message), "ADC Raw: %u, Voltage: %lu.%03luV, Threshold: %u\r\n", adc_value, voltage_mv / 1000, voltage_mv % 1000, light_dark_threshold);
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
		key_up_pressed_flag = 1;
	}
	else if (GPIO_Pin == GPIO_PIN_2)
	{
		key_down_pressed_flag = 1;
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