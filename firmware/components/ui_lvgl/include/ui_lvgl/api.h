#pragma once

#include "pet/hal/display.h"
#include "pet/hal/input.h"

/* 单任务拥有 LVGL，BSP 由组合根注入；不会从 MQTT 或事件总线回调绘制。 */
typedef uint32_t (*pet_ui_clock_t)(void);
esp_err_t pet_ui_start(const pet_display_t *display, const pet_button_input_t *button,
                       pet_ui_clock_t clock);

typedef void (*pet_ui_recovery_restart_t)(void *context);
/* Recovery startup only (UI + RECOVERY enabled); no business services are initialized.
 * restart runs once after two visible holds and stable release; it must not erase user data. */
esp_err_t pet_ui_recovery_start(const pet_display_t *,const pet_button_input_t *,
                                 pet_ui_recovery_restart_t,void *context);

typedef struct {uint32_t frames,failed_frames,last_frame_ms,max_frame_ms;} pet_ui_metrics_t;
void pet_ui_metrics(pet_ui_metrics_t *out);
