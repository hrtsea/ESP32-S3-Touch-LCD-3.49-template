#include "snmp_client.h"
#include "config.h"
#include "nas_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "snmp_client";

#define SNMP_RX_BUF_SIZE 512
#define SNMP_TX_BUF_SIZE 256

static const uint8_t OID_SYSNAME[] = {0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x05, 0x00};
static const uint8_t OID_SYSUPTIME[] = {0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00};
static const uint8_t OID_HRPROCLOAD[] = {0x2B, 0x06, 0x01, 0x02, 0x01, 0x25, 0x03, 0x03, 0x01, 0x02, 0x01};
static const uint8_t OID_IFINOCTETS[] = {0x2B, 0x06, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x0A, 0x02};
static const uint8_t OID_IFOUTOCTETS[] = {0x2B, 0x06, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x10, 0x02};

typedef enum {
    SNMP_IDLE,
    SNMP_QUERY_SYSNAME,
    SNMP_QUERY_UPTIME,
    SNMP_QUERY_CPU,
    SNMP_QUERY_IF_IN,
    SNMP_QUERY_IF_OUT,
    SNMP_WAIT_RESPONSE,
    SNMP_DONE
} SnmpState;

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    char community[32];
    NasData data;
    uint32_t last_poll_ms;
    uint32_t last_rx_bytes;
    uint32_t last_tx_bytes;
    uint32_t last_net_time;
    SnmpState state;
    SnmpState last_query;
    uint16_t request_id;
    uint8_t rx_buf[SNMP_RX_BUF_SIZE];
    int rx_len;
    int sock;
    bool sock_created;
} SnmpClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    return err == ESP_OK;
}

static size_t encode_length(uint16_t len, uint8_t* buf)
{
    if (len < 128) {
        buf[0] = (uint8_t)len;
        return 1;
    }
    buf[0] = 0x82;
    buf[1] = (len >> 8) & 0xFF;
    buf[2] = len & 0xFF;
    return 3;
}

static size_t decode_length(const uint8_t* buf, size_t* out_len)
{
    if (buf[0] < 128) {
        *out_len = buf[0];
        return 1;
    }
    size_t num_bytes = buf[0] & 0x7F;
    size_t len = 0;
    for (size_t i = 0; i < num_bytes; i++) {
        len = (len << 8) | buf[1 + i];
    }
    *out_len = len;
    return 1 + num_bytes;
}

static int decode_integer(const uint8_t* buf, size_t len)
{
    int val = 0;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) | buf[i];
    }
    return val;
}

static uint32_t decode_counter(const uint8_t* buf, size_t len)
{
    uint32_t val = 0;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) | buf[i];
    }
    return val;
}

static bool send_get(SnmpClientData* priv, const uint8_t* oid, size_t oid_len)
{
    uint8_t tx_buf[SNMP_TX_BUF_SIZE];
    size_t pos = 0;

    priv->request_id++;

    uint8_t varbind[64];
    size_t vb_pos = 0;
    varbind[vb_pos++] = 0x30;
    vb_pos++;

    varbind[vb_pos++] = 0x06;
    varbind[vb_pos++] = (uint8_t)oid_len;
    memcpy(varbind + vb_pos, oid, oid_len);
    vb_pos += oid_len;

    varbind[vb_pos++] = 0x05;
    varbind[vb_pos++] = 0x00;

    varbind[1] = (uint8_t)(vb_pos - 2);

    uint8_t vblist[68];
    vblist[0] = 0x30;
    vblist[1] = (uint8_t)vb_pos;
    memcpy(vblist + 2, varbind, vb_pos);
    size_t vblist_len = 2 + vb_pos;

    uint8_t pdu[128];
    size_t pdu_pos = 0;
    pdu[pdu_pos++] = 0xA0;
    pdu_pos++;

    pdu[pdu_pos++] = 0x02;
    pdu[pdu_pos++] = 0x02;
    pdu[pdu_pos++] = (priv->request_id >> 8) & 0xFF;
    pdu[pdu_pos++] = priv->request_id & 0xFF;

    pdu[pdu_pos++] = 0x02;
    pdu[pdu_pos++] = 0x01;
    pdu[pdu_pos++] = 0x00;

    pdu[pdu_pos++] = 0x02;
    pdu[pdu_pos++] = 0x01;
    pdu[pdu_pos++] = 0x00;

    memcpy(pdu + pdu_pos, vblist, vblist_len);
    pdu_pos += vblist_len;
    pdu[1] = (uint8_t)(pdu_pos - 2);

    tx_buf[pos++] = 0x30;
    pos++;

    tx_buf[pos++] = 0x02;
    tx_buf[pos++] = 0x01;
    tx_buf[pos++] = 0x01;

    size_t comm_len = strlen(priv->community);
    tx_buf[pos++] = 0x04;
    tx_buf[pos++] = (uint8_t)comm_len;
    memcpy(tx_buf + pos, priv->community, comm_len);
    pos += comm_len;

    memcpy(tx_buf + pos, pdu, pdu_pos);
    pos += pdu_pos;
    tx_buf[1] = (uint8_t)(pos - 2);

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(priv->nas_port);
    inet_pton(AF_INET, priv->nas_ip, &dest_addr.sin_addr);

    ssize_t sent = sendto(priv->sock, tx_buf, pos, 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    return sent == (ssize_t)pos;
}

static bool wait_for_response(SnmpClientData* priv, uint32_t timeout_ms)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(priv->sock, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(priv->sock + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(priv->sock, &readfds)) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        priv->rx_len = recvfrom(priv->sock, priv->rx_buf, sizeof(priv->rx_buf), 0,
                                (struct sockaddr*)&from_addr, &from_len);
        return priv->rx_len > 0;
    }
    return false;
}

static bool find_value_in_response(SnmpClientData* priv, const uint8_t* target_oid, size_t oid_len,
                                   const uint8_t** value_out, size_t* value_len, uint8_t* tag_out)
{
    size_t pos = 0;

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x30) return false;
    size_t msg_len;
    pos += decode_length(priv->rx_buf + pos, &msg_len);

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x02) return false;
    size_t ver_len;
    pos += decode_length(priv->rx_buf + pos, &ver_len);
    pos += ver_len;

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x04) return false;
    size_t comm_len;
    pos += decode_length(priv->rx_buf + pos, &comm_len);
    pos += comm_len;

    if ((size_t)priv->rx_len < pos + 1) return false;
    pos++;
    size_t pdu_len;
    pos += decode_length(priv->rx_buf + pos, &pdu_len);

    for (int i = 0; i < 3; i++) {
        if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x02) return false;
        size_t int_len;
        pos += decode_length(priv->rx_buf + pos, &int_len);
        pos += int_len;
    }

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x30) return false;
    size_t vbl_len;
    pos += decode_length(priv->rx_buf + pos, &vbl_len);

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x30) return false;
    size_t vb_len;
    pos += decode_length(priv->rx_buf + pos, &vb_len);

    if ((size_t)priv->rx_len < pos + 1 || priv->rx_buf[pos++] != 0x06) return false;
    size_t resp_oid_len;
    pos += decode_length(priv->rx_buf + pos, &resp_oid_len);

    bool oid_match = (resp_oid_len == oid_len);
    if (oid_match) {
        for (size_t i = 0; i < oid_len; i++) {
            if (priv->rx_buf[pos + i] != target_oid[i]) {
                oid_match = false;
                break;
            }
        }
    }

    pos += resp_oid_len;

    if ((size_t)priv->rx_len < pos + 1) return false;
    *tag_out = priv->rx_buf[pos++];
    size_t val_len;
    pos += decode_length(priv->rx_buf + pos, &val_len);

    if (oid_match && pos + val_len <= (size_t)priv->rx_len) {
        *value_out = priv->rx_buf + pos;
        *value_len = val_len;
        return true;
    }

    return false;
}

static bool snmp_init(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)calloc(1, sizeof(SnmpClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = 161;

    if (g_config.snmp_comm[0] != '\0') {
        memcpy(priv->community, g_config.snmp_comm, sizeof(priv->community) - 1);
        priv->community[sizeof(priv->community) - 1] = '\0';
    } else {
        memcpy(priv->community, "public", sizeof("public"));
    }

    priv->state = SNMP_IDLE;
    priv->last_query = SNMP_IDLE;
    priv->request_id = (uint16_t)(esp_log_timestamp() % 65535);
    priv->rx_len = 0;
    priv->sock = -1;
    priv->sock_created = false;
    memset(&priv->data, 0, sizeof(priv->data));

    ESP_LOGI(TAG, "Init: %s:%d (community=%s)", priv->nas_ip, priv->nas_port, priv->community);
    return strlen(priv->nas_ip) > 0;
}

static bool snmp_connect(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)self->priv;

    priv->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (priv->sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(priv->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    priv->sock_created = true;
    priv->data.is_online = true;
    priv->state = SNMP_IDLE;
    ESP_LOGI(TAG, "UDP socket opened");
    return true;
}

static void snmp_disconnect(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)self->priv;
    if (priv && priv->sock_created && priv->sock >= 0) {
        close(priv->sock);
        priv->sock = -1;
        priv->sock_created = false;
    }
    priv->data.is_online = false;
    priv->state = SNMP_IDLE;
    ESP_LOGI(TAG, "Disconnected");
}

static bool snmp_poll(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)self->priv;

    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    uint32_t now = get_millis();
    if (priv->last_poll_ms > 0 && (now - priv->last_poll_ms) < g_config.poll_sec * 1000UL) {
        return false;
    }

    uint8_t tag;
    const uint8_t* val;
    size_t val_len;

    switch (priv->state) {
        case SNMP_IDLE:
            priv->state = SNMP_QUERY_SYSNAME;

        case SNMP_QUERY_SYSNAME:
            ESP_LOGI(TAG, "Querying sysName...");
            send_get(priv, OID_SYSNAME, sizeof(OID_SYSNAME));
            priv->last_query = SNMP_QUERY_SYSNAME;
            priv->state = SNMP_WAIT_RESPONSE;
            break;

        case SNMP_QUERY_UPTIME:
            ESP_LOGI(TAG, "Querying sysUpTime...");
            send_get(priv, OID_SYSUPTIME, sizeof(OID_SYSUPTIME));
            priv->last_query = SNMP_QUERY_UPTIME;
            priv->state = SNMP_WAIT_RESPONSE;
            break;

        case SNMP_QUERY_CPU:
            ESP_LOGI(TAG, "Querying hrProcessorLoad...");
            send_get(priv, OID_HRPROCLOAD, sizeof(OID_HRPROCLOAD));
            priv->last_query = SNMP_QUERY_CPU;
            priv->state = SNMP_WAIT_RESPONSE;
            break;

        case SNMP_QUERY_IF_IN:
            ESP_LOGI(TAG, "Querying ifInOctets...");
            send_get(priv, OID_IFINOCTETS, sizeof(OID_IFINOCTETS));
            priv->last_query = SNMP_QUERY_IF_IN;
            priv->state = SNMP_WAIT_RESPONSE;
            break;

        case SNMP_QUERY_IF_OUT:
            ESP_LOGI(TAG, "Querying ifOutOctets...");
            send_get(priv, OID_IFOUTOCTETS, sizeof(OID_IFOUTOCTETS));
            priv->last_query = SNMP_QUERY_IF_OUT;
            priv->state = SNMP_WAIT_RESPONSE;
            break;

        case SNMP_WAIT_RESPONSE:
            if (wait_for_response(priv, 500)) {
                if (find_value_in_response(priv, OID_SYSNAME, sizeof(OID_SYSNAME), &val, &val_len, &tag)) {
                    if (tag == 0x04 && val_len > 0) {
                        size_t copy_len = val_len;
                        if (copy_len >= sizeof(priv->data.system.hostname)) copy_len = sizeof(priv->data.system.hostname) - 1;
                        memcpy(priv->data.system.hostname, val, copy_len);
                        priv->data.system.hostname[copy_len] = '\0';
                        ESP_LOGI(TAG, "Hostname: %s", priv->data.system.hostname);
                    }
                }

                if (find_value_in_response(priv, OID_SYSUPTIME, sizeof(OID_SYSUPTIME), &val, &val_len, &tag)) {
                    if (tag == 0x02 || tag == 0x43) {
                        uint32_t uptime = decode_counter(val, val_len);
                        priv->data.system.uptime_s = uptime / 100;
                        ESP_LOGI(TAG, "Uptime: %lu seconds", priv->data.system.uptime_s);
                    }
                }

                if (find_value_in_response(priv, OID_HRPROCLOAD, sizeof(OID_HRPROCLOAD), &val, &val_len, &tag)) {
                    if (tag == 0x02) {
                        priv->data.system.cpu_pct = (float)decode_integer(val, val_len);
                        ESP_LOGI(TAG, "CPU Load: %.1f%%", priv->data.system.cpu_pct);
                    }
                }

                if (find_value_in_response(priv, OID_IFINOCTETS, sizeof(OID_IFINOCTETS), &val, &val_len, &tag)) {
                    if (tag == 0x01 || tag == 0x41) {
                        uint32_t rx_bytes = decode_counter(val, val_len);
                        if (priv->last_net_time > 0) {
                            uint32_t dt = (now - priv->last_net_time) / 1000;
                            if (dt > 0) {
                                priv->data.network.rx_bps = (rx_bytes - priv->last_rx_bytes) / dt;
                            }
                        }
                        priv->last_rx_bytes = rx_bytes;
                    }
                }

                if (find_value_in_response(priv, OID_IFOUTOCTETS, sizeof(OID_IFOUTOCTETS), &val, &val_len, &tag)) {
                    if (tag == 0x01 || tag == 0x41) {
                        uint32_t tx_bytes = decode_counter(val, val_len);
                        if (priv->last_net_time > 0) {
                            uint32_t dt = (now - priv->last_net_time) / 1000;
                            if (dt > 0) {
                                priv->data.network.tx_bps = (tx_bytes - priv->last_tx_bytes) / dt;
                            }
                        }
                        priv->last_tx_bytes = tx_bytes;
                    }
                }

                priv->last_net_time = now;
                priv->data.is_online = true;
            } else {
                ESP_LOGW(TAG, "Response timeout");
            }

            switch (priv->last_query) {
                case SNMP_QUERY_SYSNAME: priv->state = SNMP_QUERY_UPTIME; break;
                case SNMP_QUERY_UPTIME: priv->state = SNMP_QUERY_CPU; break;
                case SNMP_QUERY_CPU: priv->state = SNMP_QUERY_IF_IN; break;
                case SNMP_QUERY_IF_IN: priv->state = SNMP_QUERY_IF_OUT; break;
                case SNMP_QUERY_IF_OUT:
                    priv->data.last_update_ms = now;
                    priv->data.has_update = true;
                    priv->state = SNMP_DONE;
                    ESP_LOGI(TAG, "Poll complete");
                    break;
                default: priv->state = SNMP_QUERY_SYSNAME; break;
            }
            break;

        case SNMP_DONE:
            priv->state = SNMP_QUERY_SYSNAME;
            priv->last_poll_ms = now;
            return false;
    }

    priv->last_poll_ms = now;
    return priv->data.has_update;
}

static bool snmp_is_connected(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)self->priv;
    return priv->data.is_online;
}

static const NasData* snmp_get_data(DataSource* self)
{
    SnmpClientData* priv = (SnmpClientData*)self->priv;
    return &priv->data;
}

static const char* snmp_get_type_name(DataSource* self)
{
    (void)self;
    return "SNMP";
}

static const char* snmp_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig snmp_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NET_SNMP);
}

static void snmp_destroy(DataSource* self)
{
    if (self && self->priv) {
        SnmpClientData* priv = (SnmpClientData*)self->priv;
        if (priv->sock_created && priv->sock >= 0) {
            close(priv->sock);
        }
        free(priv);
        self->priv = NULL;
    }
    if (self) free(self);
}

static const DataSourceVTable s_snmp_vtable = {
    .init = snmp_init,
    .connect = snmp_connect,
    .disconnect = snmp_disconnect,
    .poll = snmp_poll,
    .is_connected = snmp_is_connected,
    .get_data = snmp_get_data,
    .get_type_name = snmp_get_type_name,
    .get_conn_icon = snmp_get_conn_icon,
    .get_config = snmp_get_config,
    .destroy = snmp_destroy,
};

DataSource* snmp_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_snmp_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}
