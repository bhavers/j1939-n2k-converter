#pragma once

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "config.h"

namespace RemoteLogger {

static int s_server_sock = -1;
static int s_client_sock = -1;
static SemaphoreHandle_t s_log_mutex = nullptr;
static bool s_running = false;

static void tcp_server_task(void *pvParameters) {
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(TELNET_PORT);

    s_server_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (s_server_sock < 0) {
        ESP_LOGE("TCP_MON", "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(s_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int err = bind(s_server_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE("TCP_MON", "Socket unable to bind: errno %d", errno);
        close(s_server_sock);
        vTaskDelete(NULL);
        return;
    }

    err = listen(s_server_sock, 1);
    if (err != 0) {
        ESP_LOGE("TCP_MON", "Error occurred during listen: errno %d", errno);
        close(s_server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI("TCP_MON", "Socket listening on port %d", TELNET_PORT);

    while (s_running) {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(s_server_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            // Keep serving: a failed accept (e.g. no free socket) must not end the monitor.
            ESP_LOGE("TCP_MON", "Unable to accept connection: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI("TCP_MON", "Client connected from %s", addr_str);

        if (s_log_mutex && xSemaphoreTake(s_log_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_client_sock >= 0) {
                close(s_client_sock);
            }
            s_client_sock = sock;
            xSemaphoreGive(s_log_mutex);
        }

        const char *welcome = "\r\n=== J1939-N2K Gateway TCP Monitor (ESP32-C5) ===\r\n\r\n";
        int sent = send(sock, welcome, strlen(welcome), 0);
        ESP_LOGI("TCP_MON", "Welcome sent: %d bytes (errno %d)", sent, sent < 0 ? errno : 0);

        char rx_buffer[128];
        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len <= 0) {
                ESP_LOGI("TCP_MON", "Client disconnected (recv %d, errno %d)", len, len < 0 ? errno : 0);
                break;
            }
        }

        if (s_log_mutex && xSemaphoreTake(s_log_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_client_sock == sock) {
                close(s_client_sock);
                s_client_sock = -1;
            }
            xSemaphoreGive(s_log_mutex);
        }
    }

    vTaskDelete(NULL);
}

inline void init() {
    if (!s_log_mutex) {
        s_log_mutex = xSemaphoreCreateMutex();
    }
    s_running = true;
    xTaskCreate(tcp_server_task, "tcp_mon_task", 4096, NULL, 2, NULL);
}

inline void logf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    printf("%s", buf);

    if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_client_sock >= 0) {
            send(s_client_sock, buf, strlen(buf), 0);
        }
        xSemaphoreGive(s_log_mutex);
    }
}

} // namespace RemoteLogger
