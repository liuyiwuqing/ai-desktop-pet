#pragma once
#include "esp_err.h"
#include "pet_ext/actuator.h"
typedef enum {PET_SERVO_CMD_MOVE,PET_SERVO_CMD_HOME,PET_SERVO_CMD_WAG,PET_SERVO_CMD_NOD} pet_servo_action_t;
typedef struct {uint64_t request_id;pet_servo_result_t result;pet_servo_state_t state;bool configured;} pet_servo_service_view_t;
/* 板级 GPIO 约束由组合根注入，扩展不依赖某块 BSP。 */
esp_err_t pet_servo_service_start(bool (*gpio_available)(int));
/* 仅物理维护入口调用；排队成功表示已接受，不代表舵机已到位。 */
esp_err_t pet_servo_service_arm(void);
esp_err_t pet_servo_service_submit(uint64_t request,pet_servo_action_t action,int16_t angle,uint16_t duration);
esp_err_t pet_servo_service_stop(bool emergency);
esp_err_t pet_servo_service_snapshot(pet_servo_service_view_t *out);
