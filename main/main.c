#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "hal/uart_types.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "driver/uart.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK


// Firebase credentials
#define FIREBASE_HOST  "iot-firebase-80171-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_API_KEY "AIzaSyA3wUxLxeGNnfy6OZgcEDDg9NHXwzLEikQ"
// Tag for logging
static const char *TAG = "Firebase";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

const uart_port_t uart_num = UART_NUM_2;
static int s_retry_num = 0;
/* test variables*/


/* TLS Root Certificate for Firebase */
extern const uint8_t _binary_firebase_cert_pem_start[] asm("_binary_firebase_cert_pem_start");
extern const uint8_t _binary_firebase_cert_pem_end[] asm("_binary_firebase_cert_pem_end");

static void event_handler(void* arg, esp_event_base_t event_base,
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
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Galaxym31",
            .password = "12345678",
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
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void UART_Init(void)
{
	
uart_config_t uart_config = {
    .baud_rate = 38400,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
};
// Configure UART parameters
ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

// Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 4, 5, 18, 19));

// Setup UART buffered IO with event queue
const int uart_buffer_size = (1024 * 2);
QueueHandle_t uart_queue;
// Install UART driver using an event queue here
ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, \
                                        uart_buffer_size, 10, &uart_queue, 0));
}




esp_err_t client_event_get_handler(esp_http_client_event_handle_t http_event)
{
	switch (http_event->event_id) {
		case HTTP_EVENT_ON_DATA:
		printf("HTTP_EVENT_ON_DATA: %.*s\n", http_event->data_len, (char*)http_event->data);
		break;
		default:
		break;
    }
	return ESP_OK;
	
}
/* Function to push data to Firebase */
void push_data_to_firebase(char* data) {

     
    printf("Sending JSON data: %s\n", data);
    
    esp_http_client_config_t config = {
        .url = "https://firestore.googleapis.com/v1/projects/iot-firebase-80171/databases/(default)/documents/collection/1JRc9F32B4n1fWYeRlHT",
        .cert_pem = (char *)_binary_firebase_cert_pem_start,  // Set CA cert for SSL
        .event_handler = client_event_get_handler
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);   //To create new document send post, to update field send patch
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, data, strlen(data));


    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    
  
    
}

/* Function to get data from Firebase */
void get_data_from_firebase(void) {

    esp_http_client_config_t config = {
        .url = "https://firestore.googleapis.com/v1/projects/iot-firebase-80171/databases/(default)/documents/collection/1JRc9F32B4n1fWYeRlHT",
        .cert_pem = (char *)_binary_firebase_cert_pem_start,  // Set CA cert for SSL
        .event_handler = client_event_get_handler
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d", esp_http_client_get_status_code(client));
        char buffer[512];
        esp_http_client_read(client, buffer, sizeof(buffer));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    
    esp_http_client_cleanup(client);
    
}
// Application main task
void app_main() {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    UART_Init();
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    
    // Push data to Firebase
 /*   char json_data[200];
    sprintf(json_data, "{\"fields\":{\"roll\":{\"doubleValue\":%d},\"pitch\":{\"doubleValue\":%d}}}",roll, pitch);    
    push_data_to_firebase( json_data); 
    ESP_LOGI(TAG, "DATA PUSHED TO THE FIRESTORE"); */
    
    //Retrieve data from Firebase
    //get_data_from_firebase();
    
    // Read data from UART.
	const uart_port_t uart_num = UART_NUM_2;
	uint8_t data[35];
	int length = 0;
	while(1){
		
	
	ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));
	length = uart_read_bytes(uart_num, data, length, 100);
	if (length > 0) {
    data[length] = '\0';  // Null-terminate the data
    }
      
    int8_t roll ;
    int8_t pitch ;
    ESP_LOGI(TAG, "UART Received Data: %s", data);
    // Parse the incoming string using sscanf
    sscanf((char*)data, "Roll:  %hhd         Pitch:  %hhd   \n\r", &roll, &pitch);
        
    // Log the parsed values
    ESP_LOGI("Parsed Data", "Roll: %d, Pitch: %d", roll, pitch);
   
    // Push data to Firebase
    char json_data[100];
    sprintf(json_data, "{\"fields\":{\"roll\":{\"doubleValue\":%d},\"pitch\":{\"doubleValue\":%d}}}",roll, pitch);    
    push_data_to_firebase( json_data); 
    ESP_LOGI(TAG, "DATA PUSHED TO THE FIRESTORE"); 
    
    }
}