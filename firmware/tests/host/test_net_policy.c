#include <stdio.h>
#include <string.h>

#include "net_policy.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

static void test_device_id(void)
{
    CHECK(pet_net_device_id_valid("pet-dev-01"));
    CHECK(pet_net_device_id_valid("A1"));
    CHECK(pet_net_device_id_valid("pet.dev_2"));
    CHECK(!pet_net_device_id_valid(NULL));
    CHECK(!pet_net_device_id_valid(""));
    CHECK(!pet_net_device_id_valid("-pet"));
    CHECK(!pet_net_device_id_valid("pet/dev"));
    CHECK(!pet_net_device_id_valid("pet+dev"));
    CHECK(!pet_net_device_id_valid("pet#dev"));
    CHECK(!pet_net_device_id_valid("pet dev"));

    char id[PET_NET_DEVICE_ID_MAX + 2];
    memset(id, 'a', sizeof(id));
    id[PET_NET_DEVICE_ID_MAX + 1] = '\0';
    CHECK(!pet_net_device_id_valid(id));

    id[PET_NET_DEVICE_ID_MAX] = '\0';
    CHECK(pet_net_device_id_valid(id));
}

static void test_topics(void)
{
    char topic[PET_NET_TOPIC_MAX];

    CHECK(pet_net_topic_build(PET_NET_TOPIC_INBOX, "pet-dev-01", topic,
                              sizeof(topic)));
    CHECK(strcmp(topic, "nexus/v1/devices/pet-dev-01/inbox") == 0);
    CHECK(pet_net_topic_build(PET_NET_TOPIC_ACK, "pet-dev-01", topic,
                              sizeof(topic)));
    CHECK(strcmp(topic, "nexus/v1/devices/pet-dev-01/ack") == 0);
    CHECK(pet_net_topic_build(PET_NET_TOPIC_STATUS, "pet-dev-01", topic,
                              sizeof(topic)));
    CHECK(strcmp(topic, "nexus/v1/devices/pet-dev-01/status") == 0);
    CHECK(pet_net_topic_build(PET_NET_TOPIC_CONFIG, "pet-dev-01", topic,
                              sizeof(topic)));
    CHECK(strcmp(topic, "nexus/v1/devices/pet-dev-01/config") == 0);

    char small[8];
    CHECK(!pet_net_topic_build(PET_NET_TOPIC_INBOX, "pet-dev-01", small,
                               sizeof(small)));
    CHECK(small[0] == '\0');
    CHECK(!pet_net_topic_build(PET_NET_TOPIC_INBOX, "bad/id", topic,
                               sizeof(topic)));
    CHECK(!pet_net_topic_build(PET_NET_TOPIC_COUNT, "pet-dev-01", topic,
                               sizeof(topic)));
    CHECK(!pet_net_topic_build(PET_NET_TOPIC_INBOX, "pet-dev-01", NULL,
                               sizeof(topic)));
}

static pet_net_config_t base_config(void)
{
    pet_net_config_t config = {
        .wifi_ssid = "home",
        .wifi_password = "password123",
        .broker_uri = "mqtts://broker.local:8883",
        .device_id = "pet-dev-01",
        .mqtt_username = "device",
        .mqtt_password = "token",
        .ca_cert_pem = NULL,
        .keepalive_s = 60,
        .reconnect_timeout_ms = 2000,
    };
    return config;
}

static void test_config_validation(void)
{
    pet_net_config_t config = base_config();
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);
    CHECK(pet_net_config_validate(NULL) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.wifi_password = NULL;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    config = base_config();
    config.wifi_password = "";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    config = base_config();
    config.wifi_password = "short";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.wifi_ssid = NULL;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.wifi_ssid = "";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    char ssid[PET_NET_SSID_MAX + 2];
    memset(ssid, 's', sizeof(ssid));
    ssid[PET_NET_SSID_MAX + 1] = '\0';
    config = base_config();
    config.wifi_ssid = ssid;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    ssid[PET_NET_SSID_MAX] = '\0';
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    config = base_config();
    config.device_id = "bad/id";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.broker_uri = "mqtt://broker.local:1883";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.broker_uri = "mqtts://";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.broker_uri = "mqtts:///path";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.broker_uri = "mqtts://h";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    char user[PET_NET_USERNAME_MAX + 2];
    memset(user, 'u', sizeof(user));
    user[PET_NET_USERNAME_MAX + 1] = '\0';
    config = base_config();
    config.mqtt_username = user;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);
    user[PET_NET_USERNAME_MAX] = '\0';
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    config = base_config();
    config.ca_cert_pem = "not a certificate";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config.ca_cert_pem = "-----BEGIN CERTIFICATE-----\nMIIB\n"
                         "-----END CERTIFICATE-----\n";
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    char pem[PET_NET_CA_PEM_MAX + 2];
    memset(pem, 'a', sizeof(pem));
    pem[PET_NET_CA_PEM_MAX + 1] = '\0';
    config.ca_cert_pem = pem;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);

    config = base_config();
    config.keepalive_s = 9;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);
    config.keepalive_s = 3601;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);
    config.keepalive_s = 3600;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);

    config = base_config();
    config.reconnect_timeout_ms = 499;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);
    config.reconnect_timeout_ms = 120001;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_CONFIG);
    config.reconnect_timeout_ms = 120000;
    CHECK(pet_net_config_validate(&config) == PET_NET_ERROR_NONE);
}

static void test_names(void)
{
    CHECK(strcmp(pet_net_state_name(PET_NET_STATE_ONLINE), "online") == 0);
    CHECK(strcmp(pet_net_state_name(PET_NET_STATE_COUNT), "unknown") == 0);
    CHECK(strcmp(pet_net_error_name(PET_NET_ERROR_TLS), "tls") == 0);
    CHECK(strcmp(pet_net_error_name(PET_NET_ERROR_COUNT), "unknown") == 0);
}

int main(void)
{
    test_device_id();
    test_topics();
    test_config_validation();
    test_names();

    if (s_failures != 0) {
        printf("%d check(s) failed\n", s_failures);
        return 1;
    }
    printf("test_net_policy: all checks passed\n");
    return 0;
}
