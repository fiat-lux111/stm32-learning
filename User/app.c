#include "app.h"
#include "main.h"

void app_main(void){
	while (1)
	{
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
		HAL_Delay(30);
	}
}