#include "pet_ext/actuator.h"
#include <string.h>

bool pet_servo_profile_valid(const pet_servo_profile_t *p)
{
    return p&&p->id>0&&p->gpio>=0&&p->gpio<=48&&p->min_us>=500&&p->min_us<=1000&&
        p->max_us>=2000&&p->max_us<=2500&&p->min_angle>=0&&p->max_angle<=180&&p->min_angle<p->max_angle&&
        p->safe_position>=p->min_angle&&p->safe_position<=p->max_angle&&p->max_speed_dps>=1&&p->max_speed_dps<=180;
}
static int index_of(const pet_servo_engine_t *s,uint8_t id)
{for(size_t i=0;i<s->count;++i)if(s->profiles[i].id==id)return (int)i;return -1;}
static uint16_t pulse_for(const pet_servo_profile_t *p,int16_t angle)
{
    if(p->inverted)angle=180-angle;
    return (uint16_t)(p->min_us+(uint32_t)angle*(p->max_us-p->min_us)/180);
}
static pet_servo_result_t fault(pet_servo_engine_t *s)
{
    s->fault=true;s->armed=false;s->sequence_count=0;
    (void)s->port.detach(s->port.context);
    for(size_t i=0;i<s->count;++i){s->states[i].moving=false;s->states[i].armed=false;s->states[i].fault=true;}
    return PET_SERVO_IO;
}
pet_servo_result_t pet_servo_engine_init(pet_servo_engine_t *s,const pet_servo_profile_t *profiles,size_t count,
    const pet_actuator_port_t *port,uint64_t persisted)
{
    if(!s||!profiles||!count||count>2||!port||!port->attach||!port->pulse||!port->detach||!port->reserve)return PET_SERVO_INVALID;
    for(size_t i=0;i<count;++i){
        if(!pet_servo_profile_valid(&profiles[i]))return PET_SERVO_INVALID;
        for(size_t j=0;j<i;++j)if(profiles[j].id==profiles[i].id||profiles[j].gpio==profiles[i].gpio)return PET_SERVO_INVALID;
    }
    *s=(pet_servo_engine_t){.count=count,.port=*port,.last_request_id=persisted};
    memcpy(s->profiles,profiles,count*sizeof(*profiles));
    for(size_t i=0;i<count;++i){s->states[i].angle=profiles[i].safe_position;s->states[i].last_request_id=persisted;}
    return PET_SERVO_OK;
}
pet_servo_result_t pet_servo_arm(pet_servo_engine_t *s,bool confirmed)
{
    if(!s||!s->count)return PET_SERVO_INVALID;
    if(!confirmed)return PET_SERVO_CONFIRM;
    if(s->armed)return PET_SERVO_OK;
    if(!s->port.attach(s->port.context,s->profiles,s->count))return fault(s);
    for(size_t i=0;i<s->count;++i) {
        if(!s->port.pulse(s->port.context,s->profiles[i].id,pulse_for(&s->profiles[i],s->profiles[i].safe_position)))return fault(s);
        s->states[i]=(pet_servo_state_t){.angle=s->profiles[i].safe_position,.armed=true,.last_request_id=s->last_request_id};
    }
    s->armed=true;s->emergency=s->fault=false;return PET_SERVO_OK;
}
static pet_servo_result_t validate(const pet_servo_profile_t *p,int16_t from,
    const pet_servo_keyframe_t *frames,size_t count,bool confirmed)
{
    unsigned total=0;
    for(size_t i=0;i<count;++i) {
        const pet_servo_keyframe_t *f=&frames[i];int delta=f->angle-from;if(delta<0)delta=-delta;
        if(f->angle<p->min_angle||f->angle>p->max_angle||f->duration_ms<100||f->duration_ms>5000||
            f->hold_ms>2000||(unsigned)delta*1000>(unsigned)f->duration_ms*p->max_speed_dps)return PET_SERVO_INVALID;
        if(!confirmed&&(delta>90||(unsigned)delta*1000>(unsigned)f->duration_ms*120))return PET_SERVO_CONFIRM;
        total+=f->duration_ms+f->hold_ms;if(total>10000)return PET_SERVO_INVALID;
        from=f->angle;
    }
    return PET_SERVO_OK;
}
static bool same_command(const pet_servo_engine_t *s,uint8_t id,const pet_servo_keyframe_t *frames,size_t count)
{
    if(s->last_command_id!=id||s->last_command_count!=count)return false;
    for(size_t i=0;i<count;++i)if(frames[i].angle!=s->last_command[i].angle||
        frames[i].duration_ms!=s->last_command[i].duration_ms||frames[i].hold_ms!=s->last_command[i].hold_ms)return false;
    return true;
}
pet_servo_result_t pet_servo_sequence(pet_servo_engine_t *s,uint64_t request,uint8_t id,
    const pet_servo_keyframe_t *frames,size_t count,uint64_t now,bool confirmed)
{
    if(!s||!s->count||!request||!frames||!count||count>8)return PET_SERVO_INVALID;
    int index=index_of(s,id);if(index<0)return PET_SERVO_INVALID;
    if(request<=s->last_request_id)return request==s->last_request_id&&same_command(s,id,frames,count)?PET_SERVO_DUPLICATE:PET_SERVO_CONFLICT;
    if(s->emergency)return PET_SERVO_ESTOP;
    if(!s->armed)return PET_SERVO_DISARMED;
    if(s->sequence_count)return PET_SERVO_BUSY;
    if(now<s->last_tick_ms)return PET_SERVO_INVALID;
    pet_servo_result_t result=validate(&s->profiles[index],s->states[index].angle,frames,count,confirmed);
    if(result!=PET_SERVO_OK)return result;
    if(!s->port.reserve(s->port.context,request))return PET_SERVO_IO;
    s->last_request_id=request;s->last_command_id=id;s->last_command_count=(uint8_t)count;
    memcpy(s->last_command,frames,count*sizeof(*frames));memcpy(s->sequence,frames,count*sizeof(*frames));
    s->sequence_count=(uint8_t)count;s->sequence_index=0;s->active_id=id;
    s->from_angle=s->states[index].angle;s->target_angle=frames[0].angle;s->duration_ms=frames[0].duration_ms;
    s->started_ms=s->last_tick_ms=now;s->states[index].moving=true;s->states[index].last_request_id=request;
    return PET_SERVO_OK;
}
pet_servo_result_t pet_servo_move(pet_servo_engine_t *s,uint64_t request,uint8_t id,int16_t angle,
    uint16_t duration,uint64_t now,bool confirmed)
{const pet_servo_keyframe_t frame={angle,duration,0};return pet_servo_sequence(s,request,id,&frame,1,now,confirmed);}
pet_servo_result_t pet_servo_tick(pet_servo_engine_t *s,uint64_t now)
{
    if(!s||!s->count)return PET_SERVO_INVALID;
    if(s->emergency)return PET_SERVO_ESTOP;
    if(s->fault)return PET_SERVO_IO;
    if(now<s->last_tick_ms)return fault(s);
    if(s->sequence_count&&now-s->last_tick_ms>100)return fault(s);
    s->last_tick_ms=now;
    if(!s->sequence_count)return PET_SERVO_OK;
    int index=index_of(s,s->active_id);if(index<0)return fault(s);
    uint64_t elapsed=now-s->started_ms;
    uint32_t t=elapsed>s->duration_ms?s->duration_ms:(uint32_t)elapsed;
    int16_t angle=(int16_t)(s->from_angle+((int32_t)s->target_angle-s->from_angle)*(int32_t)t/s->duration_ms);
    if(angle!=s->states[index].angle) {
        if(!s->port.pulse(s->port.context,s->active_id,pulse_for(&s->profiles[index],angle)))return fault(s);
        s->states[index].angle=angle;
    }
    uint16_t hold=s->sequence[s->sequence_index].hold_ms;
    if(elapsed>=s->duration_ms+hold) {
        if(++s->sequence_index>=s->sequence_count) {s->sequence_count=0;s->states[index].moving=false;}
        else {
            s->from_angle=angle;s->target_angle=s->sequence[s->sequence_index].angle;
            s->duration_ms=s->sequence[s->sequence_index].duration_ms;s->started_ms=now;
        }
    }
    return PET_SERVO_OK;
}
pet_servo_result_t pet_servo_stop(pet_servo_engine_t *s,bool emergency)
{
    if(!s||!s->count)return PET_SERVO_INVALID;
    s->sequence_count=0;
    for(size_t i=0;i<s->count;++i)s->states[i].moving=false;
    if(emergency) {
        s->emergency=true;s->armed=false;
        for(size_t i=0;i<s->count;++i){s->states[i].emergency=true;s->states[i].armed=false;}
        if(!s->port.detach(s->port.context))return fault(s);
    }
    return PET_SERVO_OK;
}
bool pet_servo_get_state(const pet_servo_engine_t *s,uint8_t id,pet_servo_state_t *out)
{
    if(!s||!out)return false;
    int index=index_of(s,id);if(index<0)return false;*out=s->states[index];return true;
}
