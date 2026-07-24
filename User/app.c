#include "app.h"
#include "main.h"
#include "oled.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;

static volatile uint8_t key_up_pressed_flag = 0;
static volatile uint8_t key_down_pressed_flag = 0;
static volatile uint8_t tim2_tick_flag = 0;
static volatile uint8_t uart_command_ready = 0;
static volatile uint8_t uart_rx_need_restart = 0;

#define LIGHT_THRESHOLD_STEP 100U
#define LIGHT_THRESHOLD_MIN 100U
#define LIGHT_THRESHOLD_MAX 4000U
#define PWM_DUTY_STEP 10U
#define PWM_DUTY_MAX 40U
#define PWM_SMOOTH_STEP 2U
#define ADC_FILTER_SAMPLE_COUNT 10U
#define APP_CONFIG_FLASH_ADDR 0x0800FC00UL
#define APP_CONFIG_MAGIC 0x43464731UL
#define APP_CONFIG_CHECK_VALUE 0xA5A55A5AUL
#define UART_DMA_RX_BUFFER_SIZE 64U
#define UART_COMMAND_BUFFER_SIZE 64U

static uint16_t light_dark_threshold = 2500;
static uint8_t pwm_duty_percent = 50;
static uint8_t pwm_target_duty_percent = 50;
static uint8_t pwm_auto_mode = 0;
static uint8_t uart_dma_rx_buffer[UART_DMA_RX_BUFFER_SIZE];
static char uart_command_buffer[UART_COMMAND_BUFFER_SIZE];

static void app_pwm_set_duty(uint8_t duty_percent);
static void app_pwm_set_target_duty(uint8_t duty_percent);
static void app_uart_start_dma_receive(void);

typedef struct
{
	uint32_t magic;
	uint16_t threshold;
	uint8_t pwm_auto_mode;
	uint8_t pwm_target_duty;
	uint32_t checksum;
} app_config_t;

static uint32_t app_config_calc_checksum(const app_config_t *config)
{
	return config->magic ^ config->threshold ^ config->pwm_auto_mode ^ config->pwm_target_duty ^ APP_CONFIG_CHECK_VALUE;
}

static void app_config_apply_default(void)
{
	light_dark_threshold = 2500;
	pwm_auto_mode = 0;
	app_pwm_set_target_duty(40);
	pwm_duty_percent = pwm_target_duty_percent;
}

static uint8_t app_config_load(void)
{
	const app_config_t *config = (const app_config_t *)APP_CONFIG_FLASH_ADDR;

	if (config->magic != APP_CONFIG_MAGIC)
	{
		app_config_apply_default();
		return 0;
	}

	if (config->checksum != app_config_calc_checksum(config))
	{
		app_config_apply_default();
		return 0;
	}

	if (config->threshold < LIGHT_THRESHOLD_MIN || config->threshold > LIGHT_THRESHOLD_MAX ||
		config->pwm_target_duty > PWM_DUTY_MAX || config->pwm_auto_mode > 1)
	{
		app_config_apply_default();
		return 0;
	}

	light_dark_threshold = config->threshold;
	pwm_auto_mode = config->pwm_auto_mode;
	app_pwm_set_target_duty(config->pwm_target_duty);
	pwm_duty_percent = pwm_target_duty_percent;
	return 1;
}

static HAL_StatusTypeDef app_config_save(void)
{
	app_config_t config;
	FLASH_EraseInitTypeDef erase_init;
	uint32_t page_error = 0;
	HAL_StatusTypeDef status;

	config.magic = APP_CONFIG_MAGIC;
	config.threshold = light_dark_threshold;
	config.pwm_auto_mode = pwm_auto_mode;
	config.pwm_target_duty = pwm_target_duty_percent;
	config.checksum = app_config_calc_checksum(&config);

	HAL_FLASH_Unlock();

	erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_init.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_init.NbPages = 1;
	status = HAL_FLASHEx_Erase(&erase_init, &page_error);

	if (status == HAL_OK)
	{
		const uint16_t *data = (const uint16_t *)&config;
		for (uint32_t i = 0; i < sizeof(config) / sizeof(uint16_t); i++)
		{
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, APP_CONFIG_FLASH_ADDR + i * sizeof(uint16_t), data[i]);
			if (status != HAL_OK)
			{
				break;
			}
		}
	}

	HAL_FLASH_Lock();
	return status;
}

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

static void app_pwm_set_target_duty(uint8_t duty_percent)
{
	if (duty_percent > PWM_DUTY_MAX)
	{
		duty_percent = PWM_DUTY_MAX;
	}

	pwm_target_duty_percent = duty_percent;
}

static void app_pwm_update_smooth(void)
{
	if (pwm_duty_percent < pwm_target_duty_percent)
	{
		uint8_t next_duty = pwm_target_duty_percent;
		if (pwm_target_duty_percent - pwm_duty_percent > PWM_SMOOTH_STEP)
		{
			next_duty = pwm_duty_percent + PWM_SMOOTH_STEP;
		}
		app_pwm_set_duty(next_duty);
	}
	else if (pwm_duty_percent > pwm_target_duty_percent)
	{
		uint8_t next_duty = pwm_target_duty_percent;
		if (pwm_duty_percent - pwm_target_duty_percent > PWM_SMOOTH_STEP)
		{
			next_duty = pwm_duty_percent - PWM_SMOOTH_STEP;
		}
		app_pwm_set_duty(next_duty);
	}
}

static void app_pwm_duty_up(void)
{
	if (pwm_target_duty_percent + PWM_DUTY_STEP < PWM_DUTY_MAX)
	{
		app_pwm_set_target_duty(pwm_target_duty_percent + PWM_DUTY_STEP);
	}
	else
	{
		app_pwm_set_target_duty(PWM_DUTY_MAX);
	}
}

static void app_pwm_duty_down(void)
{
	if (pwm_target_duty_percent > PWM_DUTY_STEP)
	{
		app_pwm_set_target_duty(pwm_target_duty_percent - PWM_DUTY_STEP);
	}
	else
	{
		app_pwm_set_target_duty(0);
	}
}

static void app_pwm_set_by_adc(uint16_t adc_value)
{
	uint8_t duty_percent = (uint8_t)(adc_value * 100UL / 4095UL);
	app_pwm_set_target_duty(duty_percent);
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

static uint16_t app_read_adc_average(void)
{
	uint32_t adc_sum = 0;

	for (uint8_t i = 0; i < ADC_FILTER_SAMPLE_COUNT; i++)
	{
		adc_sum += app_read_adc();
		HAL_Delay(2);
	}

	return (uint16_t)(adc_sum / ADC_FILTER_SAMPLE_COUNT);
}

static void app_uart_print_help(void)
{
	const char *help =
		"Commands:\r\n"
		"help              show help\r\n"
		"status            show status\r\n"
		"threshold <100-4000>\r\n"
		"pwm <0-40>\r\n"
		"mode auto|manual\r\n"
		"save              save config\r\n"
		"reset             reset default\r\n"
		"Legacy: 1 ? a + - t u d p m s r\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)help, strlen(help), HAL_MAX_DELAY);
}

static void app_uart_start_dma_receive(void)
{
	HAL_UART_DMAStop(&huart1);
	__HAL_UART_CLEAR_IDLEFLAG(&huart1);
	__HAL_UART_CLEAR_OREFLAG(&huart1);
	memset(uart_dma_rx_buffer, 0, sizeof(uart_dma_rx_buffer));
	HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_dma_rx_buffer, UART_DMA_RX_BUFFER_SIZE);
	__HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
}

static void app_uart_print_status(void)
{
	char message[128];
	uint16_t adc_value = app_read_adc_average();
	uint32_t voltage_mv = adc_value * 3300UL / 4095UL;

	snprintf(message, sizeof(message), "Status: ADC=%u, Voltage=%lu.%03luV, Threshold=%u, PWM=%u%%->%u%%, Mode=%s\r\n",
		adc_value, voltage_mv / 1000, voltage_mv % 1000, light_dark_threshold, pwm_duty_percent, pwm_target_duty_percent, pwm_auto_mode ? "Auto" : "Manual");
	HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
}

static void app_command_trim_and_lower(char *command)
{
	char *start = command;
	char *end;
	char *line_end;

	line_end = strpbrk(command, "\r\n");
	if (line_end != NULL)
	{
		*line_end = '\0';
	}

	while (*start != '\0' && isspace((unsigned char)*start))
	{
		start++;
	}

	if (start != command)
	{
		memmove(command, start, strlen(start) + 1U);
	}

	end = command + strlen(command);
	while (end > command && isspace((unsigned char)*(end - 1)))
	{
		end--;
	}
	*end = '\0';

	for (char *p = command; *p != '\0'; p++)
	{
		*p = (char)tolower((unsigned char)*p);
	}
}

static void app_process_command(char *command)
{
	char message[128];

	app_command_trim_and_lower(command);
	if (command[0] == '\0')
	{
		return;
	}

	if (strcmp(command, "1") == 0)
	{
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		const char *reply = "LED Toggle\r\n";
		HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "?") == 0 || strcmp(command, "help") == 0)
	{
		app_uart_print_help();
	}
	else if (strcmp(command, "a") == 0 || strcmp(command, "status") == 0)
	{
		app_uart_print_status();
	}
	else if (strcmp(command, "+") == 0)
	{
		app_threshold_up();
		snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "-") == 0)
	{
		app_threshold_down();
		snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "t") == 0)
	{
		snprintf(message, sizeof(message), "Threshold: %u\r\n", light_dark_threshold);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strncmp(command, "threshold ", 10) == 0)
	{
		long value = strtol(command + 10, NULL, 10);
		if (value >= LIGHT_THRESHOLD_MIN && value <= LIGHT_THRESHOLD_MAX)
		{
			light_dark_threshold = (uint16_t)value;
			snprintf(message, sizeof(message), "Threshold set: %u\r\n", light_dark_threshold);
		}
		else
		{
			snprintf(message, sizeof(message), "Invalid threshold, range: %u-%u\r\n", LIGHT_THRESHOLD_MIN, LIGHT_THRESHOLD_MAX);
		}
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "u") == 0)
	{
		pwm_auto_mode = 0;
		app_pwm_duty_up();
		snprintf(message, sizeof(message), "PWM Manual, Duty: %u%%, Target: %u%%\r\n", pwm_duty_percent, pwm_target_duty_percent);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "d") == 0)
	{
		pwm_auto_mode = 0;
		app_pwm_duty_down();
		snprintf(message, sizeof(message), "PWM Manual, Duty: %u%%, Target: %u%%\r\n", pwm_duty_percent, pwm_target_duty_percent);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "p") == 0)
	{
		snprintf(message, sizeof(message), "PWM %s, Duty: %u%%, Target: %u%%\r\n", pwm_auto_mode ? "Auto" : "Manual", pwm_duty_percent, pwm_target_duty_percent);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strncmp(command, "pwm ", 4) == 0)
	{
		long value = strtol(command + 4, NULL, 10);
		if (value >= 0 && value <= PWM_DUTY_MAX)
		{
			pwm_auto_mode = 0;
			app_pwm_set_target_duty((uint8_t)value);
			snprintf(message, sizeof(message), "PWM target set: %u%%\r\n", pwm_target_duty_percent);
		}
		else
		{
			snprintf(message, sizeof(message), "Invalid PWM, range: 0-%u\r\n", PWM_DUTY_MAX);
		}
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strncmp(command, "pwm", 3) == 0 && isdigit((unsigned char)command[3]))
	{
		long value = strtol(command + 3, NULL, 10);
		if (value >= 0 && value <= PWM_DUTY_MAX)
		{
			pwm_auto_mode = 0;
			app_pwm_set_target_duty((uint8_t)value);
			snprintf(message, sizeof(message), "PWM target set: %u%%\r\n", pwm_target_duty_percent);
		}
		else
		{
			snprintf(message, sizeof(message), "Invalid PWM, range: 0-%u\r\n", PWM_DUTY_MAX);
		}
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "m") == 0)
	{
		pwm_auto_mode = !pwm_auto_mode;
		snprintf(message, sizeof(message), "PWM Mode: %s\r\n", pwm_auto_mode ? "Auto" : "Manual");
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "mode auto") == 0)
	{
		pwm_auto_mode = 1;
		const char *reply = "PWM Mode: Auto\r\n";
		HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "mode manual") == 0)
	{
		pwm_auto_mode = 0;
		const char *reply = "PWM Mode: Manual\r\n";
		HAL_UART_Transmit(&huart1, (uint8_t *)reply, strlen(reply), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "s") == 0 || strcmp(command, "save") == 0)
	{
		if (app_config_save() == HAL_OK)
		{
			snprintf(message, sizeof(message), "Config saved: Threshold=%u, PWM=%u%%, Mode=%s\r\n", light_dark_threshold, pwm_target_duty_percent, pwm_auto_mode ? "Auto" : "Manual");
		}
		else
		{
			snprintf(message, sizeof(message), "Config save failed\r\n");
		}
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else if (strcmp(command, "r") == 0 || strcmp(command, "reset") == 0)
	{
		app_config_apply_default();
		app_pwm_set_duty(pwm_target_duty_percent);
		snprintf(message, sizeof(message), "Config reset default: Threshold=%u, PWM=%u%%, Mode=%s\r\n", light_dark_threshold, pwm_target_duty_percent, pwm_auto_mode ? "Auto" : "Manual");
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
	else
	{
		snprintf(message, sizeof(message), "Unknown command: %s\r\n", command);
		HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	}
}

static void app_oled_show_status(uint16_t adc_value, uint32_t voltage_mv)
{
	char line[24];

	oled_clear();
	oled_set_cursor(0, 0);
	snprintf(line, sizeof(line), "ADC:%u", adc_value);
	oled_write_string(line);

	oled_set_cursor(0, 2);
	snprintf(line, sizeof(line), "V:%lu.%03luV", voltage_mv / 1000, voltage_mv % 1000);
	oled_write_string(line);

	oled_set_cursor(0, 4);
	snprintf(line, sizeof(line), "PWM:%u->%u%%", pwm_duty_percent, pwm_target_duty_percent);
	oled_write_string(line);

	oled_set_cursor(0, 6);
	snprintf(line, sizeof(line), "MODE:%s", pwm_auto_mode ? "AUTO" : "MANUAL");
	oled_write_string(line);
	oled_update();
}

void app_main(void){
	uint32_t key_up_count = 0;
	uint32_t key_down_count = 0;
	uint32_t tim2_count = 0;
	uint32_t last_adc_time = 0;
	uint32_t last_alarm_blink_time = 0;
	uint8_t light_alarm_active = 0;
	uint8_t config_loaded = 0;
	char message[96];
	HAL_UART_Transmit(&huart1, (uint8_t*)"NEW FIRMWARE\r\n", 15, HAL_MAX_DELAY);

	const char *start_message = "ADC + PWM test start\r\nType help to show commands\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t *)start_message, strlen(start_message), HAL_MAX_DELAY);
	config_loaded = app_config_load();
	snprintf(message, sizeof(message), "Config %s, Threshold: %u, PWM Target: %u%%, Mode: %s\r\n", config_loaded ? "loaded" : "default", light_dark_threshold, pwm_target_duty_percent, pwm_auto_mode ? "Auto" : "Manual");
	HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
	oled_init(&hi2c1);
	oled_set_cursor(0, 0);
	oled_write_string("HELLO OLED");
	oled_set_cursor(0, 2);
	oled_write_string("STM32 I2C OK");
	oled_update();
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	app_pwm_set_target_duty(pwm_target_duty_percent);
	app_pwm_set_duty(pwm_target_duty_percent);
	//HAL_TIM_Base_Start_IT(&htim2);
	memset(uart_dma_rx_buffer, 0, sizeof(uart_dma_rx_buffer));
	memset(uart_command_buffer, 0, sizeof(uart_command_buffer));
	app_uart_start_dma_receive();

	while (1)
	{
		if (uart_command_ready)
		{
			uart_command_ready = 0;
			app_process_command(uart_command_buffer);
			memset(uart_command_buffer, 0, sizeof(uart_command_buffer));
		}

		if (uart_rx_need_restart)
		{
			uart_rx_need_restart = 0;
			app_uart_start_dma_receive();
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
			uint16_t adc_value = app_read_adc_average();
			uint32_t voltage_mv = adc_value * 3300UL / 4095UL;
			uint8_t is_dark = (adc_value > light_dark_threshold);
			if (pwm_auto_mode)
			{
				app_pwm_set_by_adc(adc_value);
			}

			snprintf(message, sizeof(message), "ADC Avg: %u, Voltage: %lu.%03luV, Threshold: %u, PWM: %u%%->%u%% %s\r\n", adc_value, voltage_mv / 1000, voltage_mv % 1000, light_dark_threshold, pwm_duty_percent, pwm_target_duty_percent, pwm_auto_mode ? "Auto" : "Manual");
			HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
			app_oled_show_status(adc_value, voltage_mv);

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

		app_pwm_update_smooth();
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

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart->Instance == USART1)
	{
		uint16_t copy_size = Size;
		char *line_end;
		if (copy_size >= UART_COMMAND_BUFFER_SIZE)
		{
			copy_size = UART_COMMAND_BUFFER_SIZE - 1U;
		}

		memcpy(uart_command_buffer, uart_dma_rx_buffer, copy_size);
		uart_command_buffer[copy_size] = '\0';
		line_end = strpbrk(uart_command_buffer, "\r\n");
		if (line_end != NULL)
		{
			*line_end = '\0';
		}
		uart_command_ready = 1;
		uart_rx_need_restart = 1;
	}
}