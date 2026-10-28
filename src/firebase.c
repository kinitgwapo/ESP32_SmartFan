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
static esp_err_t firebase_sign_in(void)
{
    char url[256];

    int url_length = snprintf(
        url,
        sizeof(url),
        "https://identitytoolkit.googleapis.com/v1/"
        "accounts:signInWithPassword?key=%s",
        FIREBASE_API_KEY
    );

    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        ESP_LOGE(TAG, "Authentication URL is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *request_json = cJSON_CreateObject();
    if (request_json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool json_ok =
        cJSON_AddStringToObject(
            request_json, "email", FIREBASE_AUTH_EMAIL) != NULL &&
        cJSON_AddStringToObject(
            request_json, "password", FIREBASE_AUTH_PASSWORD) != NULL &&
        cJSON_AddBoolToObject(
            request_json, "returnSecureToken", true) != NULL;

    if (!json_ok) {
        cJSON_Delete(request_json);
        return ESP_ERR_NO_MEM;
    }

    char *request_body = cJSON_PrintUnformatted(request_json);
    cJSON_Delete(request_json);

    if (request_body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    http_response_t response = {0};

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = auth_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        free(request_body);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_set_header(
        client, "Content-Type", "application/json");

    if (err == ESP_OK) {
        err = esp_http_client_set_post_field(
            client, request_body, (int)strlen(request_body));
    }

    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }

    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(request_body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Authentication HTTP request failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (response.overflow) {
        ESP_LOGE(TAG, "Authentication response was too large");
        return ESP_ERR_INVALID_SIZE;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Authentication failed; HTTP status: %d",
                 status);
        ESP_LOGE(TAG, "Check credentials, API key and Firebase Auth settings");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response.body);
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not parse authentication response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *token =
        cJSON_GetObjectItemCaseSensitive(root, "idToken");

    if (!cJSON_IsString(token) ||
        token->valuestring == NULL ||
        token->valuestring[0] == '\0') {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Authentication response did not contain an ID token");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /*
     * Checkpoint A only: verify token presence.
     * Later stages will retain the token securely in memory
     * and use it for authenticated Realtime Database requests.
     */
    ESP_LOGI(TAG, "Firebase Authentication succeeded; ID token received");

    cJSON_Delete(root);
    return ESP_OK;
}







static void firebase_auth_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while(!network_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "WiFi connected. Testing Firebase Authentication");
    esp_err_t err = firebase_sign_in();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Firebase Authentication test failed");
    }

    vTaskDelete(NULL);
}

esp_err_t firebase_start_auth_test(void) {
    BaseType_t result = xTaskCreate(
        firebase_auth_test_task,
        "firebase_auth",
        AUTH_TASK_STACK,
        NULL,
        4,
        NULL
    );

    return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}