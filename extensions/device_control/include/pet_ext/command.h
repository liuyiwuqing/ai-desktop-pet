#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pet/extension.h"
typedef enum { PET_COMMAND_OK,PET_COMMAND_PENDING,PET_COMMAND_DENIED,PET_COMMAND_INVALID,
    PET_COMMAND_BUSY,PET_COMMAND_TIMEOUT,PET_COMMAND_UNKNOWN,PET_COMMAND_FAILED,PET_COMMAND_UNAVAILABLE } pet_command_status_t;
typedef struct {uint64_t id;char device[32],capability[64],argument[41];uint32_t turn_id;} pet_command_request_t;
typedef struct {pet_command_status_t status;char summary[128];} pet_command_result_t;
typedef struct {
    const char *device,*name;pet_ext_risk_t risk;bool confirmation;
    const char *const *arguments;size_t argument_count; /* 无参数能力使用 0 */
    uint32_t timeout_ms;
} pet_command_capability_t;
typedef struct {
    void *context;
    pet_command_status_t (*execute)(void *,const pet_command_request_t *,uint32_t,pet_command_result_t *);
    uint64_t (*now_ms)(void *);
    uint64_t (*nonce)(void *);
    /* L1/L2 必须在执行前持久化 request id；失败时不执行。恢复使用该水位。 */
    bool (*reserve)(void *,uint64_t id);
} pet_command_adapter_t;
typedef struct {uint64_t id,at_ms;pet_ext_risk_t risk;pet_command_status_t status;} pet_command_audit_t;
typedef struct {
    const pet_command_capability_t *capabilities;size_t count;
    pet_command_adapter_t adapter;
    uint64_t highest_id,confirmation_nonce,expires_ms,rate_start_ms,l2_start_ms;
    uint8_t rate_count,l2_count;
    uint32_t next_audit;
    bool pending;
    pet_command_request_t request;
    pet_command_result_t result;
    pet_command_audit_t audit[16];
} pet_command_engine_t;
/* 单业务拥有者串行使用；descriptor/adapter 生命周期覆盖 engine。 */
bool pet_command_init(pet_command_engine_t *,const pet_command_capability_t *,size_t,
    const pet_command_adapter_t *,uint64_t persisted_highest_id);
pet_command_status_t pet_command_submit(pet_command_engine_t *,const pet_command_request_t *,pet_command_result_t *);
/* 仅可信本地确认入口调用；nonce 不提供给 LLM。拒绝/过期不执行。 */
pet_command_status_t pet_command_confirm(pet_command_engine_t *,uint64_t id,uint64_t nonce,bool approved,pet_command_result_t *);
bool pet_command_pending(pet_command_engine_t *,pet_command_request_t *,uint64_t *nonce);
size_t pet_command_audit(const pet_command_engine_t *,pet_command_audit_t *,size_t capacity);
