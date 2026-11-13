#pragma once
#include <stddef.h>
#include <stdbool.h>
/* cJSON 自身的深度上限大于设备任务栈预算，先在线性扫描中拒绝深层输入。
 * 仅做资源预检；语法、字段、重复键仍由各协议解析器验证。 */
static inline bool pet_json_bounded(const char *text,size_t bytes,unsigned max_depth)
{
    if (!text||!bytes) return false;
    bool quoted=false,escaped=false;unsigned depth=0;
    for (size_t i=0;i<bytes;++i) {
        unsigned char c=(unsigned char)text[i];
        if (!c) return false;
        if (quoted) {
            if(escaped)escaped=false;
            else if(c=='\\') {
                /* cJSON 用 NUL 终止字符串；禁止转义 NUL 截短字段名或身份。 */
                if(i+5<bytes&&text[i+1]=='u'&&text[i+2]=='0'&&text[i+3]=='0'&&text[i+4]=='0'&&text[i+5]=='0')return false;
                escaped=true;
            } else if(c=='"')quoted=false;
        }
        else if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>max_depth)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}
    }
    return !depth&&!quoted;
}
