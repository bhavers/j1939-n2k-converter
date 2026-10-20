#pragma once

#include <stdio.h>
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote_logger.h"

namespace OTAManager {

static const char *TAG = "OTA_MGR";
static httpd_handle_t s_httpd = NULL;

inline void mark_app_valid() {
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New firmware verified successfully. Cancelling rollback...");
            RemoteLogger::logf("[OTA] New firmware verified. Rollback cancelled.\r\n");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

static esp_err_t update_post_handler(httpd_req_t *req) {
    // Same cross-site protection as the fuel API: a browser cannot add this header
    // on a request from another web page without a preflight, which we never answer.
    if (httpd_req_get_hdr_value_len(req, "X-Gateway") == 0) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Missing X-Gateway header");
        return ESP_FAIL;
    }
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty upload");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        RemoteLogger::logf("[OTA] Error: No update partition found!\r\n");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    RemoteLogger::logf("[OTA] Starting OTA write to partition '%s' (size %lu)...\r\n",
                       update_partition->label, update_partition->size);

    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    char ota_buf[4096];
    bool is_first_chunk = true;

    while (cur_len < total_len) {
        received = httpd_req_recv(req, ota_buf, sizeof(ota_buf));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (ota_handle != 0) {
                esp_ota_abort(ota_handle);
            }
            RemoteLogger::logf("[OTA] Error: Socket recv error (%d)\r\n", received);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        if (is_first_chunk) {
            is_first_chunk = false;
            esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
            if (err != ESP_OK) {
                RemoteLogger::logf("[OTA] esp_ota_begin failed (%s)\r\n", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
                return ESP_FAIL;
            }
        }

        esp_err_t err = esp_ota_write(ota_handle, (const void *)ota_buf, received);
        if (err != ESP_OK) {
            RemoteLogger::logf("[OTA] esp_ota_write failed (%s)\r\n", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            return ESP_FAIL;
        }
        cur_len += received;
    }

    esp_err_t err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        RemoteLogger::logf("[OTA] esp_ota_end failed (%s)\r\n", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        RemoteLogger::logf("[OTA] esp_ota_set_boot_partition failed (%s)\r\n", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition failed");
        return ESP_FAIL;
    }

    RemoteLogger::logf("[OTA] Firmware update complete (%d bytes written). Rebooting...\r\n", cur_len);
    httpd_resp_sendstr(req, "OK - Firmware updated! Rebooting in 2 seconds...");

    // Delayed restart task
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }, "ota_reboot", 2048, NULL, 5, NULL);

    return ESP_OK;
}

inline bool init_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.stack_size = 8192;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 16;  // page (3) + OTA (1) + fuel API (5) + headroom

    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        httpd_uri_t update_post_uri = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = update_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_httpd, &update_post_uri);

        RemoteLogger::logf("[OTA] HTTP Webserver started on port 80 (http://<IP>/)\r\n");
        return true;
    }

    RemoteLogger::logf("[OTA] Failed to start HTTP server!\r\n");
    return false;
}

} // namespace OTAManager
