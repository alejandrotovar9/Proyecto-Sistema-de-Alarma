/*
-Jose Tovar
-email: jatovar02@gmail.com
-E.I.E UCV
-27/02/23
*/

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_crt_bundle.h"
#include "driver/uart.h"

#include "driver/gpio.h"
#include <string.h>

#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"

//--------------------------CONSTANTES-----------------------------

//TAGS
static const char *TAG = "wifi station";
static const char *TAG1 = "HTTP_CLIENT";
static const char *TAG2 = "Sending sendMessage";
static const char *TAG3 = "UART";
static const char *TAG4 = "Areas-Sensores";

//Para el manejo de la API
/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

//Configuración  de Telegram
#define TOKEN "YOUR-TOKEN" //Token para el bot
char url_string[512] = "https://api.telegram.org/bot"; //URL para la API de Telegram

//Chat ID
//Chat ID para grupo de telegram "Grupo de Alarma": 1001819842287
#define chat_ID1 "YOUR-CHATID" //ID del grupo de Telegram para la alarma de la E.I.E UCV

//Constantes para pines de entrada y salida
#define SENSOR1 4
#define SENSOR2 22
#define LED_PIN 2
#define PIN_CRITICO 15
#define BUZZER 21
#define EMU_HUELLA 34

//Configuracion de parametros Wifi
#define EXAMPLE_ESP_WIFI_SSID      "YOUR-WIFI-SSID"
#define EXAMPLE_ESP_WIFI_PASS      "YOUR WIFI-PASSWORD"
#define EXAMPLE_ESP_MAXIMUM_RETRY  10

//UART 

#define uart_num UART_NUM_2
#define BUF_SIZE 1024*2
#define TASK_MEMORY 1024

/* evento FreeRTOS para señalizar que estamos conectados*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

//Para conteo de numero de intentos de conexion
static int s_retry_num = 0;

//Flag para conexion Wifi
int connect_flag = 0;

//Flag para UART
bool flag_UART = true;

//Para ver activacion de sensores
int count1 = 0, count2 = 0;

//Para ajustar el tipo de sensor
char tipo1 = 1, tipo2 = 0;


/* Certificado de Telegram extraido de:
 *
 * https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/blob/master/src/TelegramCertificate.h
   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/

extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

//------------------------------------CONFIGURACION WIFI--------------------------------------------

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
  
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        //Reintentos de conectarse, cumpliendo con el numero maximo de reintentos
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    //Si se conecto, los eventos IP ocurriran, por lo tanto se pueden mostrar
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "El numero IP es:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    //Creacion de Event Group de FreeRTOS
    s_wifi_event_group = xEventGroupCreate();

    //Uso del Macro de chequeo de errores, deben devolver ESP_OK
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //Creacion de 2 event handlers
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

    //Configuracion de SSID y Contrasena Wifi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start()); //Iniciamos la conexion Wifi

    ESP_LOGI(TAG, "wifi_init_sta finished."); //Fin de la inicializacion

    /* Esperandoa si la conexion se realizo exitosamente */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado en modo estacion a AP SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        connect_flag = 1;

    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Fallo al conectarse a SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

//------------------------------------MANEJO DE EVENTOS HTTP-----------------------

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer para guardar respuesta del http handler
    static int output_len;    
    //Switch para posibles eventos
    switch(evt->event_id) {
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG1, "HTTP_EVENT_REDIRECT");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG1, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG1, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
           
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG1, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                //  Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG1, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG1, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG1, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}


//---------------------Buzzer (Area no critica)---------------------------------------------

//Rutina para encendido de Buzzer

static void buzzer(void *arg){
    //Buzzer pin 21
    int count_buzzer = 0;

    //Se emiten 5 pulsos al activarse alarma local con 500ms de delay

    while(count_buzzer < 5){
        gpio_set_level(BUZZER, 1);
        vTaskDelay(500/ portTICK_PERIOD_MS); //500ms de delay entre pulsos
        gpio_set_level(BUZZER,0);
        count_buzzer++; //Aumento cuenta del contador de pulsos
    }
    printf("Esperando siguiente lectura de sensor... \n");

    vTaskDelete(NULL);
}


//-----------------------------------HTTP CON Telegram-----------------------------


//----------------------------SEND MESSAGE---------------------------------



static void https_telegram_sendMessage_perform_post(char texto[512])
{
    
	/* Formato
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=[CHANNEL_NAME]&text=[MESSAGE_TEXT]
	Grupos publicos
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=@GroupName&text=hello%20world
	Grupos privados
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=-1234567890123&text=hello%20world
	The %20 is the hexa for the space
	*/

//------------------Inicializacion-----------------------
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer para respuesta HTTP
    //int content_length = 0;
    //LIMPIO EL URL
    char url[512] = "";
    char copia_url_string[512] = ""; //Reinicio la copia
    strcat(copia_url_string, url_string); //copiando de nuevo el original

    //Configuracion del cliente HTTP con la URL y el certificado, tambien con el tipo de transporte (HTTPS)
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = output_buffer,        
    };
//------------------------COMANDO POST------------------------------------

    //POST
    //ESP_LOGW(TAG2, "Iniciare");
    //Declarando al cliente como del tipo antes mencionado y configurado
    esp_http_client_handle_t client = esp_http_client_init(&config);
  
    //Concateno el Token una sola vez. Al final se limpia el url cuando entro de nuevo
    strcat(copia_url_string,TOKEN); //copia_url_string = "https://api.telegram.org/bot+TOKEN"
    strcat(url,copia_url_string); // url = "https://api.telegram.org/bot+TOKEN"
    //Pasando el metodo
    strcat(url,"/sendMessage"); // url = "https://api.telegram.org/bot+TOKEN/sendMessage"
    ESP_LOGW(TAG2, "URL FINAL es: %s",url);
    //You set the real url for the request.
    esp_http_client_set_url(client, url); //URL FINAL


	/*Aqui se añade el texto y el chat id
	 * The format for the json for the telegram request is: 
     {"chat_id":123456789,"text":"Here goes the message"}
	  */
	// The example had this, but to add the chat id easierly I decided not to use a pointer
	//const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";

    //-------------------------Concatenacion del mensaje a enviar---------------------------------------------

	char post_data[512] = "";
    //Esta instruccion copia en post_data los datos a enviarse, formato JSON
	//sprintf(post_data,"{\"chat_id\":%s,\"text\":\"Se activo el sensor %d! Ha sido activado %d veces.\"}",chat_ID1,num_sensor,cuenta);
    sprintf(post_data,"{\"chat_id\":%s,\"text\":\"%s\"}",chat_ID1,texto);
    ESP_LOGW(TAG2, "El json es: %s",post_data);

    esp_http_client_set_method(client, HTTP_METHOD_POST); //Cambiando el metodo a POST, el default es GET
    esp_http_client_set_header(client, "Content-Type", "application/json"); //Ajustando el header a uno JSON
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    //Lleva a cabo el request y asigna el resultado a err, para posterior verificacion
    esp_err_t err = esp_http_client_perform(client); 

    //Verificacion de errores y status del comando HTTP
    if (err == ESP_OK) {
        ESP_LOGI(TAG2, "HTTP POST Status = %d",
                esp_http_client_get_status_code(client));
        ESP_LOGW(TAG2, "Desde Perform el output es: %s",output_buffer);

    } else {
        ESP_LOGE(TAG2, "HTTP POST request falló: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}


static void http_test_task1(void *pvParameters) {
    //Creating the string of the url
    // You concatenate the host with the Token so you only have to write the method

    ESP_LOGW(TAG1, "Espera 2 segundos antes de comenzar tareas de alarma...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG1, "Ejecuta rutina de POST con API de Telegram.");

    char texto1[512] = "";
    sprintf(texto1,"Se activo el Sensor %d, se ha activado %d veces.",1,count1);
    https_telegram_sendMessage_perform_post(texto1);

    ESP_LOGI(TAG1, "Finaliza envio de mensaje por Telegram.");

    printf("Esperando siguiente lectura de sensor... \n");
    vTaskDelete(NULL);
}

static void http_test_task2(void *pvParameters) {


    ESP_LOGW(TAG1, "Espera 2 segundos antes de comenzar TASK2...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    char texto2[512] = "";
    sprintf(texto2,"Se activo el Sensor %d, se ha activado %d veces.",2,count2);
    https_telegram_sendMessage_perform_post(texto2);

    ESP_LOGI(TAG1, "Finaliza envio de mensaje por Telegram.");
    printf("Esperando siguiente lectura de sensor... \n");

    vTaskDelete(NULL);
}

//------------------------------------AREA CRITICA O NO CRITICA--------------------

//Rutina para configurar los sensores

void tipos_sensores(void){

    //El sensor 1 sera por default el de alta prioridad, por estar cerca del micro y del sensor de huella
    //Sin embargo se permite cambiar esta configuracion y cambiar el sensor 2 a area critica
    //tipo1 = 0; tipo 2= 1
    //Se tienen 10 segundos para cambiar la configuracion actual o por default de los sensores

    int count_sensores = 0;
    bool flag_sensores = true;
    int nivel_pin_inicial = 0, nivel_pin_nuevo = 0;

    nivel_pin_inicial = gpio_get_level(PIN_CRITICO); //Se toma el valor actual de PIN de configuracion

    while(flag_sensores){
        ESP_LOGI(TAG4,"Configuracion de sensores como area critica. \n");
        ESP_LOGI(TAG4,"Esperando cambio en configuracion de sensores... \n");

        while(count_sensores < 10){
            vTaskDelay(1000/portTICK_PERIOD_MS);
            count_sensores++;
            printf("Cuenta: %d segundos. \n", count_sensores);
        }

        //Se lee el valor del pin al acabar el tiempo de espera.
        nivel_pin_nuevo = gpio_get_level(PIN_CRITICO);
        //Verificacion de cambios en el pin de configuracion
        if(nivel_pin_nuevo == nivel_pin_inicial){
            ESP_LOGI(TAG4,"Se mantiene la configuracion por default de las areas.\n");
            tipo1 = 1;
            tipo2 = 0;
            
            }
        else{
            ESP_LOGI(TAG4,"Se cambio la configuracion inicial de las areas. \n");
            tipo1 = 0;
            tipo2 = 1;
        }
        flag_sensores = false; //Acaba la configuracion de sensores
    }
}

//----------------------------------UART Deteccion de Huella-------------------------------------------

int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(uart_num, data, len);
    ESP_LOGI(logName, "Se enviaron %d bytes, la data enviada es: %s", txBytes, data);
    return txBytes;
}

char rx_task(void)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    char copia[512] = "";
    char data[512] = "";
    while (1){
        const int rxBytes = uart_read_bytes(uart_num, data, BUF_SIZE, 500 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = '\0';
            sprintf(copia,data); //Guardando una copia del string
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
        }
    }
    uart_flush(uart_num); //Limpiando UART
    return copia;
}

//Rutina para analisis de huella. Puede aplicarse si hay presencia de persona en Sensor de area critica.

//Asumiendo que ya fueron guardadas las huellas correspondientes usando alguna herramienta serial
//La rutina debe:
//1. Encender el Led para tomar la imagen (LedOn). Si la respuesta es OK LedOn, continuar.
//2. Buscar la huella usando el comando search + Numero de grupo. Las huellas estan guardadas en grupo 1.
//3. Verificar si la huella analizada esta en la memoria. Si la respuesta es OK Search1 ID=X, BKNum = 1, proceder.
//4. En caso que la respuesta sea NO Search, la huella no esta presente en la base de datos.
//5. Repetir este proceso 3 veces, si la respuesta siempre es negativa, activar alarma y enviar mensaje por Telegram.

static void tx_task(void *arg)
{
        char resp_1[512] = "";
        char resp_2[512] = "";   
        bool flag_led_off = true;
        int intentos = 0;

        while(flag_led_off){
            sendData(TAG3, "LedOn\r\n"); //Se enciende led para poder tomar imagen.
            sprintf(resp_1,rx_task()); //Copio en resp1 lo que se recibio por UART (Respuesta del sensor)

            //La respuesta sera "OK LedOn", solo importa el OK. Parseamos el string.
            int len = strlen(resp_1);
            char subbuff1[3];
            memcpy(subbuff1, &resp_2[len], 2); //Copiando OK en un substring
            subbuff1[3] = '\0'; //Para terminar el string

            if(strcmp(resp_1,"OK")){
                flag_led_off = false;
                ESP_LOGI(TAG3,"Se encendio el led del sensor.");
            }
            ESP_LOGI(TAG3,"El led no fue encendido exitosamente.");
        }

        while(intentos<3){
            sendData(TAG3,"search 1\r\n"); //Comienza proceso de deteccion de huella
            vTaskDelay(3000/portTICK_PERIOD_MS); //Delay para esperar a que el sensor haga la deteccion
            sprintf(resp_2,rx_task()); //Copio en resp1 lo que se recibio por UART (Respuesta del sensor)

            //La respuesta sera "OK Search1 ID=X, BKNum = 1", solo importa el OK. Parseamos el string.
            int len = strlen(resp_2);
            char subbuff2[3];
            memcpy(subbuff2, &resp_2[len], 2); //Copiando OK en un substring
            subbuff2[3] = '\0'; //Para terminar el string

            if(strcmp(subbuff2,"OK")){
                //La huella esta en la base de datos
                ESP_LOGI(TAG3,"La huella esta en la base de datos.");
                break;
            }
            else{
                //La huella no esta en la base de datos
                intentos++;
                ESP_LOGI(TAG3,"La huella no esta en la base de datos. Intentos: %d",intentos);
                if(intentos == 3){
                    //Se llego al maximo numero de intentos de ingreso, se activa alarma por telegram.
                    //Se verifica cual sensor es el de area critica con var. globales para ejecutar esa rutina
                    if(tipo1){
                        //Envio de mensaje por Telegram
                        xTaskCreatePinnedToCore(&http_test_task1, "http_test_task1", 8192*4, NULL, 5, NULL,1);
                    }
                    else{
                        //Envio de mensaje por Telegram
                        xTaskCreatePinnedToCore(&http_test_task2, "http_test_task2", 8192*4, NULL, 5, NULL,1);
                    }

                }
            }
        }
        //Apagado del led
        sendData(TAG3,"LedOff\r\n");
        ESP_LOGI(TAG3,"Termina la rutina de deteccion de huella,");
}

static void init_uart(void){

    //Configuracion del puerto serial
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_param_config(uart_num, &uart_config);
    //After setting communication parameters, configure the physical GPIO pins 
    //to which the other UART device will be connected. 
    //TX pin5, RX pin 4
    uart_set_pin(uart_num,17,16,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, BUF_SIZE,BUF_SIZE,0, NULL,0);

    ESP_LOGI(TAG3,"Configuracion Init UART completada.");
}

//---------------------------------Emulador de sensor de huella---------------------------
//Este emulador pretende mostrar las funcionalidades del sistema sin hacer uso del sensor de huellas real

static void emu_huella(void *arg){
    //Se leer el valor de una entrada en particular
    //La entrada era un valor alto, el sistema no hace nada.
    //Si alguna el valor no es alto, el sistema emite la alarma.

    //int count_emu = 0;
    int count2_emu = 0;
    int valor = 0;
    
        ESP_LOGI(TAG3, "Rutina de Lectura de Huellas.");
        printf("Se leera la huella en 5 segundos... \n");
        while(count2_emu <5){
            vTaskDelay(1000/portTICK_PERIOD_MS);
            count2_emu++;
            printf("Cuenta para leer huella: %d segundos. \n",count2_emu);
        }

        valor = gpio_get_level(EMU_HUELLA); //Leo el valor de la entrada en el pin 34
        printf("La huella ha sido analizada. \n");
        vTaskDelay(500/portTICK_PERIOD_MS);

        if(valor){
            printf("Esta en la base de datos \n");
        }
        else{
            //Se emite alarma
            printf("No esta en la base de datos \n");
            if(tipo1){
                        //Envio de mensaje por Telegram
                        xTaskCreatePinnedToCore(&http_test_task1, "http_test_task1", 8192*4, NULL, 5, NULL,1);
                    }
                    else{
                        //Envio de mensaje por Telegram
                        xTaskCreatePinnedToCore(&http_test_task2, "http_test_task2", 8192*4, NULL, 5, NULL,1);
                    }
        }
    printf("Fin de la rutina del emulador de huella. \n");
    vTaskDelete(NULL);
}



//--------------------------------------MANEJO de ISR---------------------------
int state = 0;
QueueHandle_t interputQueue;
//Handler del ISR necesario al crear una ISR nueva en el app_main
static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(interputQueue, &pinNumber, NULL);
    //printf("Entre en el interrupt handler! \n");
}

//Rutina a ejecutarse al activarse interrupcion por hardware
void Control_Task(void *params)
{
    int pinNumber;
    bool flag = true;

    while (flag)
    {

        if (xQueueReceive(interputQueue, &pinNumber, portMAX_DELAY))
        {    
            if(pinNumber == SENSOR1 && tipo1 == 1){
                //Sensor 1 configurado como area critica
                count1++;
                if(count1 == 1){
                    //Caso del primer encendido, manejo del string "vez"
                    printf("El sensor %d fue activado.  Ha sido activado %d vez. \n", 1, count1);
                    gpio_set_level(LED_PIN, gpio_get_level(SENSOR1));
                    if(connect_flag){
                        xTaskCreate(&emu_huella, "emu_huella", 1024*2, NULL, configMAX_PRIORITIES, NULL);
                        //xTaskCreatePinnedToCore(&http_test_task1, "http_test_task1", 8192*4, NULL, 5, NULL,1);
                    }
                }
                else{
                    printf("El sensor %d fue activado.  Ha sido activado %d veces. \n", 1, count1);
                    gpio_set_level(LED_PIN, gpio_get_level(SENSOR1));
                    //Rutina para cuando se enciende un sensor

                    if(connect_flag){  
                        xTaskCreate(&emu_huella, "emu_huella", 1024*2, NULL, configMAX_PRIORITIES, NULL);                     
                        //xTaskCreatePinnedToCore(&http_test_task1, "http_test_task1", 8192*4, NULL, 5, NULL,1);
                    }

                }
                
            }
            else if(pinNumber == SENSOR2 && tipo2 == 1){
                //Sensor 2 configurado como area critica
                count2++;
                if(count2 == 1){
                    printf("El sensor %d fue activado.  Ha sido activado %d vez. \n", 2, count2);
                    gpio_set_level(LED_PIN, gpio_get_level(SENSOR2));

                    if(connect_flag){
                        xTaskCreate(&emu_huella, "emu_huella", 1024*2, NULL, configMAX_PRIORITIES, NULL);
                        //xTaskCreatePinnedToCore(&http_test_task2, "http_test_task2", 8192*4, NULL, 5, NULL,1);
                    }

                }
                else{
                    printf("El sensor %d fue activado.  Ha sido activado %d veces. \n", 2, count2);
                    gpio_set_level(LED_PIN, gpio_get_level(SENSOR2));
                    //Rutina para cuando se enciende un sensor

                     if(connect_flag){
                        xTaskCreate(&emu_huella, "emu_huella", 1024*2, NULL, configMAX_PRIORITIES, NULL);
                        //xTaskCreatePinnedToCore(&http_test_task2, "http_test_task2", 8192*4, NULL, 5, NULL,1);
                    }
                }
            }
            //Rutinas para areas no criticas
            else if(pinNumber == SENSOR1 && tipo1 == 0){
                count1++;
                printf("El sensor %d fue activado.  Ha sido activado %d veces. \n", 1, count1);
                xTaskCreate(&buzzer, "buzzer", 1024*2, NULL, configMAX_PRIORITIES, NULL);
            }
            else if(pinNumber == SENSOR2 && tipo2 == 0){
                count2++;
                printf("El sensor %d fue activado.  Ha sido activado %d veces. \n", 2, count2);
                xTaskCreate(&buzzer, "buzzer", 1024*2, NULL, configMAX_PRIORITIES, NULL);
            }

        }
    }
    
}
//----------------------------------------------------

void init_hardware(void){

    //Configurando los pines como entrada o salida 
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_CRITICO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SENSOR1, GPIO_PULLDOWN_ONLY);
    gpio_set_direction(BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_direction(EMU_HUELLA,GPIO_MODE_INPUT);
    gpio_set_pull_mode(EMU_HUELLA, GPIO_PULLDOWN_ONLY);
   
    //Configurando las entradas como interrupciones y activandolas como ISR de flanco de subida
    gpio_set_direction(SENSOR1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SENSOR1, GPIO_PULLDOWN_ONLY);
    gpio_set_intr_type(SENSOR1, GPIO_INTR_POSEDGE);

    gpio_set_direction(SENSOR2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SENSOR2, GPIO_PULLDOWN_ONLY);
    gpio_pulldown_en(SENSOR2);
    gpio_set_intr_type(SENSOR2, GPIO_INTR_POSEDGE);
}

void app_main()
{
    //Inicializacion de Hardware
    init_hardware();

    //--------------------------WIFI------------------------------
    //Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //------------------Inicia protocolo Wifi-----------------------
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    printf("Delay para que se estabilice conexion a red Wifi... \n");

    vTaskDelay(200/portTICK_PERIOD_MS); //Delay
    printf("Wifi estable... \n");
    //---------------------------------------------------------------

    //Creacion de tarea o task para la ISR por hardware
    interputQueue = xQueueCreate(10, sizeof(int));
    xTaskCreate(Control_Task, "Control_Task", 2048, NULL, 1, NULL);
    
    //Creacion de las ISR para cada sensor
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SENSOR1, gpio_interrupt_handler, (void *)SENSOR1);
    gpio_isr_handler_add(SENSOR2, gpio_interrupt_handler, (void *)SENSOR2);

    //Inicializacion de UART
    init_uart();

    //Configurando sensores:
    tipos_sensores();

    //xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, 2, NULL);
    
    //xTaskCreate(rx_task, "uart_rx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);

}


