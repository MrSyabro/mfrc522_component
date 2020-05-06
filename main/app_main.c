#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "rc522.h"

static const char *TAG = "test_main";

uint8_t uid[MFRC522_MAX_LEN]; // Для хранения ID карт

void main_test_task(void *pvParameter)
{
    // Инициализируем RC522
	MFRC522_Init();
	
	// Считываем версия прошивки чипа и проверяем соединение
	uint32_t ver = MFRC522_ReadRegister(MFRC522_REG_VERSION);
	ESP_LOGI(TAG, "RC522 version: 0x%0x", ver);
	switch(ver) {
		case 0x88: ESP_LOGI(TAG, "clone"); break;
		case 0x90: ESP_LOGI(TAG, "v0.0"); break;
		case 0x91: ESP_LOGI(TAG, "v1.0"); break;
		case 0x92: ESP_LOGI(TAG, "v2.0"); break;
		case 0x12: ESP_LOGI(TAG, "counterfeit chip"); break;
		default:   ESP_LOGI(TAG, "Chip not found"); vTaskDelete(NULL);
	}
	
	// Ципл проверки карты и вывода ID, если карта считана
	uint8_t status;
	while (1){
		status = MFRC522_Check(&uid); // Проверяем карту
		
		if (status == MI_OK){
			ESP_LOGI(TAG, "MF return: ");
			for (uint8_t i = 0; uid[i]; i++)
				printf ("0x%0x ", uid[i]); // Выводим считаный ID
			printf("\n");
		}
		
		vTaskDelay(100 / portTICK_RATE_MS);
	}
	
	vTaskDelete(NULL);
}

void app_main()
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	/* MAIN CODE */
	
	vTaskDelay(100 / portTICK_RATE_MS);
  
	xTaskCreate(main_test_task, "test_task", configMINIMAL_STACK_SIZE * 4, NULL, 5, NULL);
}
