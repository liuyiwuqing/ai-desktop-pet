#include "net_policy.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char *const s_topic_names[PET_NET_TOPIC_COUNT] = {
    [PET_NET_TOPIC_INBOX] = "inbox",
    [PET_NET_TOPIC_ACK] = "acks",
    [PET_NET_TOPIC_STATUS] = "state",
    [PET_NET_TOPIC_CONFIG] = "config",
    [PET_NET_TOPIC_CONFIG_ACK] = "config_acks",
    [PET_NET_TOPIC_TELEMETRY] = "telemetry",
    [PET_NET_TOPIC_OTA] = "ota",
    [PET_NET_TOPIC_OTA_ACK] = "ota_acks",
    [PET_NET_TOPIC_OTA_STATUS] = "ota_status",
    [PET_NET_TOPIC_GROWTH_STATUS] = "growth_status",
    [PET_NET_TOPIC_COMPANION_STATUS] = "companion_status",
    [PET_NET_TOPIC_APPEARANCE] = "appearance",
    [PET_NET_TOPIC_APPEARANCE_ACK] = "appearance_acks",
};

static const char *const s_state_names[PET_NET_STATE_COUNT] = {
    [PET_NET_STATE_IDLE] = "idle",
    [PET_NET_STATE_CONNECTING] = "connecting",
    [PET_NET_STATE_ONLINE] = "online",
    [PET_NET_STATE_RECONNECTING] = "reconnecting",
    [PET_NET_STATE_OFFLINE] = "offline",
};

static const char *const s_error_names[PET_NET_ERROR_COUNT] = {
    [PET_NET_ERROR_NONE] = "none",
    [PET_NET_ERROR_CONFIG] = "config",
    [PET_NET_ERROR_WIFI] = "wifi",
    [PET_NET_ERROR_TLS] = "tls",
    [PET_NET_ERROR_MQTT] = "mqtt",
};

static size_t bounded_length(const char *text, size_t max)
{
    size_t length = 0;
    while (length < max && text[length] != '\0') {
        length++;
    }
    return length;
}

static bool has_control_char(const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char value = (unsigned char)text[i];
        if (value < 0x20 || value == 0x7f) {
            return true;
        }
    }
    return false;
}

static bool optional_text_valid(const char *text, size_t max_length)
{
    if (text == NULL) {
        return true;
    }
    size_t length = bounded_length(text, max_length + 1);
    return length <= max_length && !has_control_char(text, length);
}

bool pet_net_device_id_valid(const char *device_id)
{
    return pet_nexus_device_id_valid(device_id);
}

pet_net_error_t pet_net_config_validate(const pet_net_config_t *config)
{
    if (config == NULL) {
        return PET_NET_ERROR_CONFIG;
    }
    if (!pet_net_device_id_valid(config->device_id)) {
        return PET_NET_ERROR_CONFIG;
    }

    if (config->wifi_ssid == NULL) {
        return PET_NET_ERROR_CONFIG;
    }
    size_t ssid_length = bounded_length(config->wifi_ssid, PET_NET_SSID_MAX + 1);
    if (ssid_length == 0 || ssid_length > PET_NET_SSID_MAX ||
        has_control_char(config->wifi_ssid, ssid_length)) {
        return PET_NET_ERROR_CONFIG;
    }

    if (config->wifi_password != NULL) {
        size_t password_length = bounded_length(config->wifi_password, PET_NET_PASSWORD_MAX + 1);
        if (password_length > 0 && (password_length < 8 || password_length > PET_NET_PASSWORD_MAX)) {
            return PET_NET_ERROR_CONFIG;
        }
        if (has_control_char(config->wifi_password, password_length)) {
            return PET_NET_ERROR_CONFIG;
        }
    }

    const bool broker = config->broker_uri != NULL && config->broker_uri[0] != '\0';
    size_t uri_length = broker ? bounded_length(config->broker_uri, PET_NET_URI_MAX + 1) : 0;
    if (broker && (uri_length <= 8 || uri_length > PET_NET_URI_MAX ||
        strncmp(config->broker_uri, "mqtts://", 8) != 0 ||
        has_control_char(config->broker_uri, uri_length) ||
        config->broker_uri[8] == '\0' || config->broker_uri[8] == '/' ||
        config->broker_uri[8] == ':')) {
        return PET_NET_ERROR_CONFIG;
    }

    if (!optional_text_valid(config->mqtt_username, PET_NET_USERNAME_MAX) ||
        !optional_text_valid(config->mqtt_password, PET_NET_TOKEN_MAX)) {
        return PET_NET_ERROR_CONFIG;
    }

    if (!broker && ((config->mqtt_username && config->mqtt_username[0]) ||
                    (config->mqtt_password && config->mqtt_password[0]) || config->ca_cert_pem))
        return PET_NET_ERROR_CONFIG;

    if (config->ca_cert_pem != NULL) {
        size_t pem_length = bounded_length(config->ca_cert_pem, PET_NET_CA_PEM_MAX + 1);
        if (pem_length == 0 || pem_length > PET_NET_CA_PEM_MAX ||
            strstr(config->ca_cert_pem, "BEGIN CERTIFICATE") == NULL ||
            strstr(config->ca_cert_pem, "END CERTIFICATE") == NULL) {
            return PET_NET_ERROR_CONFIG;
        }
    }

    if (config->keepalive_s < 10 || config->keepalive_s > 3600) {
        return PET_NET_ERROR_CONFIG;
    }
    if (config->reconnect_timeout_ms < 500 || config->reconnect_timeout_ms > 120000) {
        return PET_NET_ERROR_CONFIG;
    }

    return PET_NET_ERROR_NONE;
}

bool pet_net_topic_build(pet_net_topic_t topic, const char *device_id,
                         char *out, size_t capacity)
{
    if (topic < PET_NET_TOPIC_INBOX || topic >= PET_NET_TOPIC_COUNT ||
        out == NULL || capacity == 0 || !pet_net_device_id_valid(device_id)) {
        return false;
    }

    int written = snprintf(out, capacity, PET_NEXUS_TOPIC_PREFIX "%s/%s",
                           device_id, s_topic_names[topic]);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}

int pet_net_presence_format(char *out, size_t capacity, const char *device_id,
                            bool online, uint32_t observed_at)
{
    if (out == NULL || capacity == 0 || !pet_net_device_id_valid(device_id) ||
        (online && observed_at == 0))
        return -1;
    const int length = snprintf(out, capacity,
        "{\"schemaVersion\":1,\"deviceId\":\"%s\",\"online\":%s,\"observedAt\":%" PRIu32 "}",
        device_id, online ? "true" : "false", observed_at);
    return length < 0 || (size_t)length >= capacity ? -1 : length;
}

const char *pet_net_state_name(pet_net_state_t state)
{
    if (state < PET_NET_STATE_IDLE || state >= PET_NET_STATE_COUNT) {
        return "unknown";
    }
    return s_state_names[state];
}

const char *pet_net_error_name(pet_net_error_t error)
{
    if (error < PET_NET_ERROR_NONE || error >= PET_NET_ERROR_COUNT) {
        return "unknown";
    }
    return s_error_names[error];
}
