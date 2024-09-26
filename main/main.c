/**
  ******************************************************************************
  * @file     main.c
  * @author   EREN MERT YİĞİT
  * @version  V1.0
  * @date     26/09/2024 11:48:07
  * @brief    Firebase push and pull main with functions
  ******************************************************************************
  @note 
  * - Using semaphore with tasks to protect shared buffers. Use it on critical sections.
  * - Without delay, tasks interrupts each other. Find suitable time delay. 
  * - Be carefull with heap memory usage
  ******************************************************************************
*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/_intsup.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "hal/gpio_types.h"
#include "hal/uart_types.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "driver/uart.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "freertos/semphr.h"
/* GPIO Pin 4-5 for led output*/
#define GPIO_LED1 4
#define GPIO_LED2 5
#define GPIO_PIN_SELECT (1ULL << GPIO_LED1) | (1ULL << GPIO_LED2)
/* WIFI Authentetaction*/
#define ESP_WIFI_SSID      "Galaxym31"
#define ESP_WIFI_PASS      "12345678"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK

/* Firebase credentials*/
#define FIREBASE_HOST  "iot-firebase-80171-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_API_KEY "AIzaSyA3wUxLxeGNnfy6OZgcEDDg9NHXwzLEikQ"
/* Length of UART buffer*/
int length = 0;
/**
* Structer to hold led states
*/
typedef enum{
	LED_OFF = 0,
    LED_ON = 1
}led_state_t;
typedef struct{
	led_state_t led1;
	led_state_t led2;
}led_struct_t;
led_struct_t led_struct;
/**
* Structer to hold roll pitch values
*/
typedef struct{
	int8_t roll;
	int8_t pitch;
}roll_pitch_return;
roll_pitch_return uart_return;
/* Global mutex handle*/ 
SemaphoreHandle_t xMutex;
/**
* buffer to hold respond from http request and total respond data length from chunks
*/
static char *response_buffer = NULL;         
static int total_length = 0;
/*Bool check to if http event finished*/
bool http_event_finish = false;       
/*Tag for logging*/
static const char *TAG = "Firebase";
/* FreeRTOS event group to signal */
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t firebase_event_group;
/* The event group allows multiple bits for each event, but we only care about three events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries 
 * - we got response from http client */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define PULL_EVENT_FINISHED BIT2
/*handle variables */
const uart_port_t uart_num = UART_NUM_1;
esp_http_client_handle_t client;
/*WIFI Connection retry*/
static int s_retry_num = 0;
/* TLS Root Certificate for Firebase */
extern const uint8_t _binary_firebase_cert_pem_start[] asm("_binary_firebase_cert_pem_start");
extern const uint8_t _binary_firebase_cert_pem_end[] asm("_binary_firebase_cert_pem_end");
/**
* WIFI Event handler
*/
static void WIFI_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
/**
* HTTP event handler. 
* Update according to your need on events.  
*/
esp_err_t http_event_handler_push(esp_http_client_event_handle_t http_event_push)
{
	switch (http_event_push->event_id) 
	{
        case HTTP_EVENT_ERROR:
             ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
             break;
        case HTTP_EVENT_ON_CONNECTED:
             ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
             break;
        case HTTP_EVENT_HEADER_SENT:
             ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
             break;
        case HTTP_EVENT_ON_HEADER:
             ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", http_event_push->header_key, http_event_push->header_value);
             break;
	    case HTTP_EVENT_ON_DATA:
	         break;
	    case HTTP_EVENT_ON_FINISH: 
	         ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");   
	         break;
	    case HTTP_EVENT_DISCONNECTED:
             ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
             break;
        case HTTP_EVENT_REDIRECT:
             ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
             break;
   }
    return ESP_OK;
}
/**
* HTTP Client event handler 
*/
esp_err_t http_event_handler_pull(esp_http_client_event_handle_t http_event_pull)
{
    switch (http_event_pull->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", http_event_pull->header_key, http_event_pull->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(http_event_pull->client)) {
                // Append the data receive    
            }           
            if (http_event_pull->data_len > 0) {
                char *new_buffer = (char *)realloc(response_buffer, total_length + http_event_pull->data_len + 1);         //Allocated memory for variable buffer data
                if (new_buffer == NULL) {
				    ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
                    if (response_buffer != NULL) 
                    {
                         free(response_buffer);         // Frees any previously allocated buffer
                         response_buffer = NULL;
                    }
                    return ESP_FAIL;
                }
                response_buffer = new_buffer;
                memcpy(response_buffer + total_length, http_event_pull->data, http_event_pull->data_len);
                total_length += http_event_pull->data_len;
                response_buffer[total_length] = '\0';         //NULL terminate
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            
             if (response_buffer != NULL && total_length > 0) {
                ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
                
                ESP_LOGI(TAG, "Full JSON Response: %s", response_buffer);         // Logs the full response
                
                /*
                * Check if the response contains valid JSON
                */
                if (response_buffer[0] == '{') {
					xEventGroupSetBits(firebase_event_group, PULL_EVENT_FINISHED);         //HTTP_EVENT_ON_FINISH sets PULL_EVENT_FINISHED for waiteventbit
                    
                } else {
                    ESP_LOGE(TAG, "Received non-JSON response");
                }
                }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            break;
        }
    return ESP_OK;
}
/**
* GPIO Initiliazition function
*/
void GPIO_Init(void)
{
	gpio_config_t gpio_cfg;
	gpio_cfg.mode = GPIO_MODE_OUTPUT;
	gpio_cfg.pin_bit_mask = GPIO_PIN_SELECT;
	gpio_cfg.intr_type = GPIO_INTR_DISABLE;
	gpio_config(&gpio_cfg);
}
/**
* WIFI STA Initiliazition function
*/
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
                                                        
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	         * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}
/**
* UART Initialization function
**/
void UART_Init(void)
{
/**
* Same as UART source
*/
uart_config_t uart_config = {
    .baud_rate = 38400,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
};
/* Configure UART parameters */
ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

/* Set UART pins(TX: IO17, RX: IO16, RTS: IO18, CTS: IO19) */
ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 17, 16, 18, 19));

/* Setup UART buffered IO with event queue*/
const int uart_buffer_size = (1024 * 2);
QueueHandle_t uart_queue;
/* Install UART driver using an event queue here */
ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, uart_buffer_size, \
                                        uart_buffer_size, 10, &uart_queue, 0));
}
/**
* @brief Receive specific data from UART
* This functions gets MPU6050 roll pitch data string from STM32 via UART and parse roll, pitch values
* @param none
* @retval roll_pitch_return
*/
roll_pitch_return UART_Receive_Data(void)
{
	//maybe try malloc here ?
	/**
	* Read data from UART
	*/	
	uint8_t data[1024];    //little buffer cause reset problems keep it high
	
	ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));

	length = uart_read_bytes(uart_num, data, length, 100 / portTICK_PERIOD_MS);          //uart_read_bytes returns len
	ESP_LOGI(TAG, "Bytes available in UART buffer: %d", length);
	data[length] = '\0';        // Null-terminate the data
	/*
	* If data received
	*/
    if (length > sizeof(data) - 1)
    {
    length = sizeof(data) - 1;         // Prevent out-of-bound write
    }
    ESP_LOGI(TAG, "UART Received Data: %s", data);
    
    sscanf((char*)data, "Roll:  %hhd         Pitch:  %hhd   \n\r", &uart_return.roll, &uart_return.pitch);         // Parse the incoming string using sscanf
    
    ESP_LOGI("Parsed UART Data", "Roll: %d, Pitch: %d", uart_return.roll, uart_return.pitch);         // Log the parsed values
   
    return uart_return;
}
/**
* @brief Push data to firebase 
* This function pushes a string to firestore via http
* @param none
* @retval none
*/
void push_data_to_firebase(void) {
	
    UART_Receive_Data();         //fill uart_return struct
    
    char json_data[128];
    sprintf(json_data, "{\"fields\":{\"Roll\":{\"doubleValue\":%d},\"Pitch\":{\"doubleValue\":%d}}}",uart_return.roll, uart_return.pitch);          //Create string with roll,pitch data in json format   
    /**
    * Configure HTTP 
    */
    esp_http_client_config_t config_push = {
        .url = "https://firestore.googleapis.com/v1/projects/iot-firebase-80171/databases/(default)/documents/collection/Roll_Pitch",
        .method = HTTP_METHOD_PATCH,         //To create new document send post, to update field send patch
        .cert_pem = (char *)_binary_firebase_cert_pem_start,         // Set CA cert for SSL
        .event_handler = http_event_handler_push,
    };
    client = esp_http_client_init(&config_push);
   
    if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return;
    }
   
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    
    
    esp_err_t err = esp_http_client_perform(client);        /* Perform the HTTP request*/
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
}
/**
* @brief Gets data from firebase
* This specific function sends request to pull data from firebase using http_event_handler_pull
* @param none
* @retval none
*/
void get_data_from_firebase(void) {
	
	firebase_event_group = xEventGroupCreate();
	
    esp_http_client_config_t config = {
        .url = "https://firestore.googleapis.com/v1/projects/iot-firebase-80171/databases/(default)/documents/collection/1JRc9F32B4n1fWYeRlHT",
        .cert_pem = (char *)_binary_firebase_cert_pem_start,  // Set CA cert for SSL
        .event_handler = http_event_handler_pull
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) 
    {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d", esp_http_client_get_status_code(client));
    }
    else 
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
}
/**
* @brief Task for pushing data
* This is a task to manage roll pitch data pushing to firebase 
* @param *pvParameters for freertos task no input param
* @retval none
*/
static void Push_Data_Task(void *pvParameters)
{
	
	while(1)
	{
	if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) == pdTRUE)       // Time is pdMS_TO_TICKS(100) to task to let it go after a while 
	 {
    
	push_data_to_firebase();          // Critical section: Firebase push data logic
	ESP_LOGI(TAG, "DATA PUSHED TO THE FIRESTORE");
    
    xSemaphoreGive(xMutex);        // Release the mutex
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	
}
/**
* @brief Task for pulling data
* This is a task to manage Led state data pulling from firebase 
* @param *pvParameters for freertos task no input param
* @retval none
*/
static void Get_Data_Task(void *pvParameters)
{
	
while (1) {
	
if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) == pdTRUE)
	 {    
	// Critical section: Firebase push data logic
        get_data_from_firebase();
        xEventGroupWaitBits(firebase_event_group,
            PULL_EVENT_FINISHED,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
        if(PULL_EVENT_FINISHED)
        {
        /**
        *  Critical section: Firebase push data logic
        */
                    cJSON *root = cJSON_Parse(response_buffer);
                    if (root == NULL)
                    {
                        ESP_LOGE(TAG, "Error parsing JSON data");
                    } 
                    else 
                    {
                        cJSON *fields = cJSON_GetObjectItem(root, "fields");
                        if (fields != NULL) 
                        {
                            cJSON *led1 = cJSON_GetObjectItem(fields, "led 1");
                            cJSON *led2 = cJSON_GetObjectItem(fields, "led 2");

                            if (led1 != NULL && led2 != NULL) 
                            {
                                led_struct.led1 = atoi(cJSON_GetObjectItem(led1, "integerValue")->valuestring);
                                led_struct.led2 = atoi(cJSON_GetObjectItem(led2, "integerValue")->valuestring);

                                gpio_set_level(GPIO_LED1, led_struct.led1 == LED_ON ? 1:0);        //If Condition is true ? then value X : otherwise value Y
                                gpio_set_level(GPIO_LED2, led_struct.led2 == LED_ON ? 1:0);
                            } 
                            else
                            {
                                ESP_LOGE(TAG, "Error retrieving LED data from JSON");
                            }
                        }
                        cJSON_Delete(root);
                    }
                // Free the response buffer
                free(response_buffer);
                response_buffer = NULL;
                total_length = 0;
         }
        else
         {
			 
			 ESP_LOGE(TAG, "PULL_EVENT_FINISHED bit is not set");
		 }
		 
		 xEventGroupClearBits(firebase_event_group, PULL_EVENT_FINISHED);
         
    xSemaphoreGive(xMutex);         // Release the mutex
    }                  
        vTaskDelay(1000 / portTICK_PERIOD_MS);         // Delay before the next iteration
    }
}

/**
* Application main task
*/
void app_main() {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // Create the mutex before starting any tasks
    xMutex = xSemaphoreCreateMutex();
    
    if (xMutex == NULL) 
    {
        ESP_LOGE(TAG, "Mutex creation failed!");
        return;
    }
    GPIO_Init();
    UART_Init();
    wifi_init_sta();
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    
    xTaskCreate(Push_Data_Task, "Push_To_Firebase", 2048 * 2, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(Get_Data_Task, "Get_Data_From_Firebase", 2048 * 2, NULL, configMAX_PRIORITIES - 1, NULL);
    
}