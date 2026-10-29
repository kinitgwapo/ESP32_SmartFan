#include "firebase.h"
#include "network_provision.h"

#include "esp_log.h"

#include "cJSON.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FIREBASE_DATABASE_URL "DATABASE_URL"
#define FIREBASE_API_KEY "FIREBASE_WEB_API_KEY"
#define FIREBASE_AUTH_EMAIL "AUTH_EMAIL"
#define FIREBASE_AUTH_PASSWORD "AUTH_PASS"

#define AUTH_RESPONSE_SIZE 4096
#define AUTH_TASK_STACK 8192
#define TOKEN_SIZE 2048

static const char *TAG = "Firebase";







typedef struct {
    char body[AUTH_RESPONSE_SIZE];
    size_t length;
    bool overflow;
} http_response_t;

/* Collect the HTTP response body safely. */
static esp_err_t auth_http_event_handler(
    esp_http_client_event_t *event)
{
    http_response_t *response =
        (http_response_t *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA &&
        event->data != NULL &&
        event->data_len > 0) {

        size_t available =
            sizeof(response->body) - 1 - response->length;
        size_t incoming = (size_t)event->data_len;

        if (incoming > available) {
            response->overflow = true;
            return ESP_FAIL;
        }

        memcpy(response->body + response->length,
               event->data,
               incoming);

        response->length += incoming;
        response->body[response->length] = '\0';
    }

    return ESP_OK;
}

/* Sign in and verify that Firebase returned an ID token. */
static esp_err_t firebase_sign_in(char *token_out, size_t token_capacity)
{
    char url[384];
    int n = snprintf(
        url,
        sizeof(url),
        "https://identitytoolkit.googleapis.com/v1/"
        "accounts:signInWithPassword?key=%s",
        FIREBASE_API_KEY
    );
    if (n < 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) return ESP_ERR_NO_MEM;
    bool ok =
        cJSON_AddStringToObject(
        request, "email", FIREBASE_AUTH_EMAIL) != NULL &&
        cJSON_AddStringToObject(
        request, "password", FIREBASE_AUTH_PASSWORD) != NULL &&
        cJSON_AddBoolToObject(
        request, "returnSecureToken", true) != NULL;
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }

    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (body == NULL) return ESP_ERR_NO_MEM;

    http_response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = auth_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) { free(body); return ESP_FAIL; }

    esp_err_t err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) err = esp_http_client_set_post_field(client, body, (int)strlen(body));
    if (err == ESP_OK) err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firebase sign-in HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (response.overflow) return ESP_ERR_INVALID_SIZE;
    if (status != 200) {
        ESP_LOGE(TAG, "Firebase sign-in returned HTTP %d: %s", status, response.body);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response.body);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "idToken");

    if (!cJSON_IsString(token) || token->valuestring == NULL ||
        strlen(token->valuestring) + 1 > token_capacity) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Sign-in response had no usable ID token");
        return ESP_ERR_INVALID_RESPONSE;
    }
    strlcpy(token_out, token->valuestring, token_capacity);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Firebase Authentication succeeded; ID token retained in RAM");
    return ESP_OK;
}







static void firebase_auth_listener_task(void *pvParameters) {
    while(true) {
        while(!network_is_connected()) {
            ESP_LOGI(TAG, "Waiting for WiFi before connecting to Firebase...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        char token[TOKEN_SIZE] = {0};
        esp_err_t err = firebase_sign_in(token, sizeof(token));
    }
    


    ESP_LOGI(TAG, "WiFi connected. Testing Firebase Authentication");
    esp_err_t err = firebase_sign_in();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Firebase Authentication test failed");
    }

    vTaskDelete(NULL);
}

esp_err_t firebase_start_auth_listener(void) {
    BaseType_t result = xTaskCreate(
        firebase_auth_listener_task,
        "firebase_sse",
        AUTH_TASK_STACK,
        NULL,
        4,
        NULL
    );

    return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}