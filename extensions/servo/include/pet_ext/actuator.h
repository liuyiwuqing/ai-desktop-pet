#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef enum {PET_SERVO_OK,PET_SERVO_INVALID,PET_SERVO_DISARMED,PET_SERVO_BUSY,
    PET_SERVO_CONFIRM,PET_SERVO_DUPLICATE,PET_SERVO_CONFLICT,PET_SERVO_IO,PET_SERVO_ESTOP} pet_servo_result_t;
typedef struct {
    uint8_t id;int8_t gpio;uint16_t min_us,max_us;
    int16_t min_angle,max_angle,safe_position;
    uint16_t max_speed_dps;bool inverted;
} pet_servo_profile_t;
typedef struct {
    void *context;
    bool (*attach)(void *,const pet_servo_profile_t *,size_t);
    bool (*pulse)(void *,uint8_t id,uint16_t microseconds);
    bool (*detach)(void *);
    bool (*reserve)(void *,uint64_t request_id); /* 运动前持久化去重水位 */
} pet_actuator_port_t;
typedef struct {int16_t angle;uint16_t duration_ms,hold_ms;} pet_servo_keyframe_t;
typedef struct {
    int16_t angle;bool moving,armed,emergency,fault;
    uint64_t last_request_id;
} pet_servo_state_t;
typedef struct {
    pet_servo_profile_t profiles[2];size_t count;pet_actuator_port_t port;
    pet_servo_state_t states[2];
    uint64_t last_request_id,started_ms,last_tick_ms;
    int16_t from_angle,target_angle;uint16_t duration_ms;
    uint8_t active_id,sequence_index,sequence_count;
    pet_servo_keyframe_t sequence[8];
    uint8_t last_command_id,last_command_count;
    pet_servo_keyframe_t last_command[8];
    bool armed,emergency,fault;
} pet_servo_engine_t;
bool pet_servo_profile_valid(const pet_servo_profile_t *);
pet_servo_result_t pet_servo_engine_init(pet_servo_engine_t *,const pet_servo_profile_t *,size_t,
    const pet_actuator_port_t *,uint64_t persisted_request_id);
/* 初始化不出 PWM；arm 只能由已完成电气/机械校准的本地维护入口调用。 */
pet_servo_result_t pet_servo_arm(pet_servo_engine_t *,bool local_confirmation);
pet_servo_result_t pet_servo_move(pet_servo_engine_t *,uint64_t request_id,uint8_t id,int16_t angle,
    uint16_t duration_ms,uint64_t now_ms,bool local_confirmation);
pet_servo_result_t pet_servo_sequence(pet_servo_engine_t *,uint64_t request_id,uint8_t id,
    const pet_servo_keyframe_t *,size_t count,uint64_t now_ms,bool local_confirmation);
pet_servo_result_t pet_servo_tick(pet_servo_engine_t *,uint64_t now_ms);
/* stop 清空序列并停在当前命令位置；急停撤销全部 PWM，必须重新本地 arm。 */
pet_servo_result_t pet_servo_stop(pet_servo_engine_t *,bool emergency);
bool pet_servo_get_state(const pet_servo_engine_t *,uint8_t id,pet_servo_state_t *);
