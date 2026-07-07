#include "wifi_provision_dns.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

static const char *TAG = "wifi_provision_dns";

#define DNS_PORT 53
#define DNS_SERVER_IP "192.168.4.1"

static bool s_running = false;
static int s_sock = -1;
static TaskHandle_t s_task_handle = NULL;

static void s_dns_task(void *arg)
{
    (void)arg;

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    uint8_t buf[512];

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

    while (s_running) {
        int recv_len = recvfrom(s_sock, buf, sizeof(buf), 0,
                                (struct sockaddr *)&addr, &addr_len);
        if (recv_len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (recv_len < 12) {
            continue;
        }

        uint8_t response[64];
        int response_len = 0;

        response[response_len++] = buf[0];
        response[response_len++] = buf[1];
        response[response_len++] = 0x81;
        response[response_len++] = 0x80;
        response[response_len++] = 0x00;
        response[response_len++] = 0x01;
        response[response_len++] = 0x00;
        response[response_len++] = 0x01;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;

        int qname_len = 0;
        int pos = 12;
        while (pos < recv_len && buf[pos] != 0) {
            uint8_t label_len = buf[pos++];
            if (label_len == 0xc0) {
                pos++;
                break;
            }
            response[response_len++] = label_len;
            for (int i = 0; i < label_len && pos < recv_len; i++) {
                response[response_len++] = buf[pos++];
            }
            qname_len += label_len + 1;
        }
        response[response_len++] = 0x00;

        if (pos + 4 <= recv_len) {
            response[response_len++] = buf[pos++];
            response[response_len++] = buf[pos++];
            response[response_len++] = buf[pos++];
            response[response_len++] = buf[pos++];
        }

        response[response_len++] = 0xc0;
        response[response_len++] = 0x0c;
        response[response_len++] = 0x00;
        response[response_len++] = 0x01;
        response[response_len++] = 0x00;
        response[response_len++] = 0x01;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x00;
        response[response_len++] = 0x3c;
        response[response_len++] = 0x00;
        response[response_len++] = 0x04;
        response[response_len++] = 192;
        response[response_len++] = 168;
        response[response_len++] = 4;
        response[response_len++] = 1;

        sendto(s_sock, response, response_len, 0,
               (struct sockaddr *)&addr, addr_len);
    }

    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

void wifi_provision_dns_init(void)
{
}

void wifi_provision_dns_start(void)
{
    if (s_running) return;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "failed to create socket");
        return;
    }

    int opt = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(DNS_PORT),
    };

    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "failed to bind socket");
        close(s_sock);
        s_sock = -1;
        return;
    }

    s_running = true;
    xTaskCreate(s_dns_task, "dns_server", 4096, NULL, 5, &s_task_handle);
}

void wifi_provision_dns_stop(void)
{
    if (!s_running) return;

    s_running = false;

    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}
