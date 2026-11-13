#pragma once

#include "esp_err.h"
#include <stddef.h>
#include "pet/growth.h"
#include "pet/climate.h"

typedef struct {
    uint64_t (*monotonic_ms)(void *context);
    uint32_t (*epoch_s)(void *context); /* 未对时返回 0 */
    void *context;
    int16_t utc_offset_min;
} pet_growth_clock_t;

/* init/close 仅用于启动/停止阶段，不可与其他调用并发；clock 的函数和 context
 * 必须覆盖服务生命周期。close 不隐式写盘，正常退出先调用 flush。 */
pet_growth_result_t pet_growth_init(const pet_growth_clock_t *clock);
void pet_growth_close(void);
/* 互动/结算/刷盘含同步存储，应从业务工作任务调用，不在 LVGL/音频回调调用。 */
pet_growth_result_t pet_growth_interact(const pet_growth_request_t *request);
pet_growth_result_t pet_growth_tick(void);
pet_growth_result_t pet_growth_flush(void);
/* 已滤波读数经独立业务任务传入，可能推进时间结算；不能直接在事件总线回调调用。
 * 无效/低质量读数清除影响，旧读数/重放不刷新有效期，不回算关机期间的环境。 */
pet_growth_result_t pet_growth_observe_climate(const pet_climate_reading_t *reading);
/* 非阻塞读取；存储正在结算时返回 NOT_READY，显示层保留上一快照。 */
pet_growth_result_t pet_growth_get_snapshot(pet_growth_view_t *out);

/* 固件入口：系统时钟 + 每秒结算任务。启动后由运行时拥有服务生命周期。 */
esp_err_t pet_growth_start(void);

/* 改时区不刷新每日配额；下一次正常日历推进仍受持久化日期约束。 */
pet_growth_result_t pet_growth_set_utc_offset(int16_t minutes);

typedef enum { PET_GROWTH_REWARD_DIALOGUE, PET_GROWTH_REWARD_FOCUS } pet_growth_reward_t;
/* 完成事件必须由本地拥有者产生；event_id 在每类内递增，day 取当前快照。
 * 对话 2 EXP/1 亲密，每日 50；25 分钟专注 20 EXP/10 金币，每日 8。
 * 与去重标志同一双槽事务提交，不接受流式 token 或云端自称“已完成”。 */
pet_growth_result_t pet_growth_reward(pet_growth_reward_t kind,uint64_t event_id,uint32_t day);

#define PET_GROWTH_BACKUP_MAX 256u
/* 物理维护入口使用；不含凭据、消息或对话。恢复沿用双槽事务，保留当前
 * 已发奖励去重水位、已领取成就与当前日限额，防止回档重复领奖。
 * 调用方须校验备份 deviceId；expected_revision 防止覆盖并发更新。 */
pet_growth_result_t pet_growth_export(void *out,size_t capacity,size_t *bytes,uint32_t *revision);
pet_growth_result_t pet_growth_import(const void *data,size_t bytes,uint32_t expected_revision);
