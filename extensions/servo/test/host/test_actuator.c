#include <assert.h>
#include <stdio.h>
#include "pet_ext/actuator.h"
static unsigned pulses;static uint16_t pulse;static bool attached,reserve_ok=true;static uint64_t watermark;
static bool attach(void *p,const pet_servo_profile_t *c,size_t n){(void)p;(void)c;assert(n==1);attached=true;return true;}
static bool output(void *p,uint8_t id,uint16_t us){(void)p;assert(id==1&&attached);++pulses;pulse=us;return true;}
static bool detach(void *p){(void)p;attached=false;return true;}
static bool reserve(void *p,uint64_t id){(void)p;if(!reserve_ok)return false;watermark=id;return true;}
static void tick_until(pet_servo_engine_t *s,uint64_t start,uint64_t end)
{for(uint64_t at=start+10;at<=end;at+=10)assert(pet_servo_tick(s,at)==PET_SERVO_OK);}
int main(void)
{
    pet_servo_profile_t profile={1,4,500,2500,0,180,90,90,false};
    pet_actuator_port_t port={NULL,attach,output,detach,reserve};pet_servo_engine_t s;pet_servo_state_t state;
    assert(pet_servo_engine_init(&s,&profile,1,&port,0)==PET_SERVO_OK&&!attached);
    assert(pet_servo_move(&s,1,1,135,1000,100,false)==PET_SERVO_DISARMED);
    assert(pet_servo_arm(&s,false)==PET_SERVO_CONFIRM);
    assert(pet_servo_arm(&s,true)==PET_SERVO_OK&&pulse==1500);
    assert(pet_servo_move(&s,1,1,180,100,100,false)==PET_SERVO_INVALID);
    assert(pet_servo_move(&s,1,1,135,1000,100,false)==PET_SERVO_OK&&watermark==1);
    tick_until(&s,100,600);assert(pulse==1744);
    assert(pet_servo_move(&s,1,1,135,1000,600,false)==PET_SERVO_DUPLICATE);
    assert(pet_servo_stop(&s,true)==PET_SERVO_OK&&!attached);
    unsigned before=pulses;assert(pet_servo_tick(&s,1200)==PET_SERVO_ESTOP&&pulses==before);
    assert(pet_servo_get_state(&s,1,&state)&&state.emergency&&!state.armed);
    assert(pet_servo_arm(&s,true)==PET_SERVO_OK);
    reserve_ok=false;assert(pet_servo_move(&s,2,1,120,1000,1300,false)==PET_SERVO_IO);
    assert(pet_servo_stop(&s,false)==PET_SERVO_OK);
    reserve_ok=true;
    const pet_servo_keyframe_t seq[]={{100,500,100},{90,500,0}};
    assert(pet_servo_sequence(&s,3,1,seq,2,1400,false)==PET_SERVO_OK);
    tick_until(&s,1400,1900);
    assert(pet_servo_stop(&s,false)==PET_SERVO_OK);before=pulses;
    assert(pet_servo_tick(&s,3000)==PET_SERVO_OK&&pulses==before);
    puts("Servo: explicit arm, speed, interpolation, persistent replay guard, sequence cancel and emergency stop passed");
}
