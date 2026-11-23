#include <stdio.h>
#include <string.h>

#include "pet/config_generated.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

int main(void)
{
    CHECK(PET_CFG_SCHEMA_VERSION == 1);
    CHECK(PET_CFG_TIMEZONE_MAX_LEN == 32);
    CHECK(PET_CFG_VOLUME_MIN == 0);
    CHECK(PET_CFG_VOLUME_MAX == 100);
    CHECK(PET_CFG_DEFAULT_VOLUME == 25);
    CHECK(PET_CFG_DEFAULT_BRIGHTNESS == 80);
    CHECK(PET_CFG_DND_START_MINUTE_MAX == 1439);
    CHECK(PET_CFG_DND_DEFAULT_ENABLED == false);
    CHECK(PET_CFG_DND_DEFAULT_ALLOW_CRITICAL == true);
    CHECK(PET_CFG_MESSAGE_DEFAULT_QUEUE_DEPTH == 8);
    CHECK(PET_CFG_MESSAGE_DEFAULT_DURATION_MS == 8000);
    CHECK(PET_CFG_SCREEN_TIMEOUT_S_MIN == 0);
    CHECK(PET_CFG_SCREEN_TIMEOUT_S_MAX == 3600);
    CHECK(PET_CFG_SCREEN_DEFAULT_TIMEOUT_S == 60);
    CHECK(PET_CFG_SCREEN_IDLE_BRIGHTNESS_MIN == 0);
    CHECK(PET_CFG_SCREEN_IDLE_BRIGHTNESS_MAX == 100);
    CHECK(PET_CFG_SCREEN_DEFAULT_IDLE_BRIGHTNESS == 40);
    CHECK(PET_CFG_SFX_VOLUME_MIN == 0);
    CHECK(PET_CFG_SFX_VOLUME_MAX == 100);
    CHECK(PET_CFG_DEFAULT_SFX_VOLUME == 25);

    pet_config_t config;
    memset(&config, 0, sizeof(config));
    config.schema_version = PET_CFG_SCHEMA_VERSION;
    config.language = PET_CFG_DEFAULT_LANGUAGE;
    config.volume = PET_CFG_DEFAULT_VOLUME;
    config.brightness = PET_CFG_DEFAULT_BRIGHTNESS;
    config.dnd.enabled = PET_CFG_DND_DEFAULT_ENABLED;
    config.dnd.start_minute = PET_CFG_DND_DEFAULT_START_MINUTE;
    config.dnd.end_minute = PET_CFG_DND_DEFAULT_END_MINUTE;
    config.dnd.allow_critical = PET_CFG_DND_DEFAULT_ALLOW_CRITICAL;
    config.screen.timeout_s = PET_CFG_SCREEN_DEFAULT_TIMEOUT_S;
    config.screen.idle_brightness = PET_CFG_SCREEN_DEFAULT_IDLE_BRIGHTNESS;
    config.sfx_volume = PET_CFG_DEFAULT_SFX_VOLUME;
    config.message.queue_depth = PET_CFG_MESSAGE_DEFAULT_QUEUE_DEPTH;
    config.message.duration_ms = PET_CFG_MESSAGE_DEFAULT_DURATION_MS;
    snprintf(config.timezone, sizeof(config.timezone), "%s", PET_CFG_DEFAULT_TIMEZONE);

    CHECK(config.schema_version == 1);
    CHECK(config.language == PET_CFG_LANGUAGE_ZH_CN);
    CHECK(config.volume == 25);
    CHECK(config.dnd.start_minute == 1320);
    CHECK(config.dnd.end_minute == 420);
    CHECK(config.message.queue_depth == 8);
    CHECK(config.message.duration_ms == 8000);
    CHECK(config.screen.timeout_s == 60);
    CHECK(config.screen.idle_brightness == 40);
    CHECK(config.sfx_volume == 25);
    CHECK(strcmp(config.timezone, "Asia/Shanghai") == 0);
    CHECK(sizeof(config.timezone) == PET_CFG_TIMEZONE_MAX_LEN + 1);

    if (s_failures == 0) {
        printf("host tests passed: generated device config\n");
    }
    return s_failures == 0 ? 0 : 1;
}
