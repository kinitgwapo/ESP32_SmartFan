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
#define SSE_LINE_SIZE 768
#define SSE_DATA_SIZE 2048

#define HTTP_TIMEOUT_MS 60000

static const char *TAG = "Firebase";







typedef struct {
    char body[AUTH_RESPONSE_SIZE];
    size_t length;
    bool overflow;
} http_response_t;

typedef struct {
    char line[SSE_LINE_SIZE];
    size_t line_length;
    char event_name[64];
    char data[SSE_DATA_SIZE];
    size_t data_length;
    bool force_reconnect;
} sse_context_t;

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

static esp_err_t sse_http_event_handler(esp_http_client_event_t *event)
{
    sse_context_t *ctx = (sse_context_t *)event->user_data;
    if (ctx == NULL) return ESP_OK;

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != NULL && event->data_len > 0) {
        const char *bytes = (const char *)event->data;
        for (int i = 0; i < event->data_len; ++i) {
            char ch = bytes[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                process_sse_line(ctx);
                if (ctx->force_reconnect) return ESP_FAIL;
            } else if (ctx->line_length + 1 < sizeof(ctx->line)) {
                ctx->line[ctx->line_length++] = ch;
            } else {
                ESP_LOGW(TAG, "Firebase SSE line too long; dropping line");
                ctx->line_length = 0;
            }
        }
    }
    return ESP_OK;
}

/* Phase B: open an authenticated RTDB SSE stream and log incoming fan commands. */
static esp_err_t firebase_listen_once(const char *token)
{
    char base[384];
    strlcpy(base, FIREBASE_DATABASE_URL, sizeof(base));
    size_t base_len = strlen(base);
    while (base_len > 0 && base[base_len - 1] == '/') base[--base_len] = '\0';

    char url[3000];
    int n = snprintf(url, sizeof(url), "%s/fan/command.json?auth=%s", base, token);
    if (n < 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    sse_context_t ctx = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = sse_http_event_handler,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_FAIL;

    esp_err_t err = esp_http_client_set_header(client, "Accept", "text/event-stream");
    if (err == ESP_OK) err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ctx.force_reconnect) return ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SSE connection ended: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Firebase SSE returned HTTP %d (check database URL, token, and rules)", status);
        return ESP_FAIL;
    }
    /* An SSE stream is expected to remain open; if it ends normally, reconnect. */
    ESP_LOGW(TAG, "Firebase SSE stream ended; reconnecting");
    return ESP_FAIL;
}





static void firebase_auth_listener_task(void *pvParameters) {
    while(true) {
        while(!network_is_connected()) {
            ESP_LOGI(TAG, "Waiting for WiFi before connecting to Firebase...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        char token[TOKEN_SIZE] = {0};
        esp_err_t err = firebase_sign_in(token, sizeof(token));
        if(err == ESP_OK) {
            ESP_LOGI(TAG, "Listening for updates at /fan/command");
            err = firebase_listen_once(token);
        }
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