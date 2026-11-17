#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* 家庭网络地址的共享规则：凭据校验、配网页面和实际 netif 配置都走这里。
 * 只做纯字符串判断，不访问 lwIP，便于主机测试。 */

#define PET_NETCFG_IP_MAX 15
#define PET_NETCFG_HOSTNAME_MAX 32
#define PET_NETCFG_DNS_COUNT 1

/* 严格点分十进制：四段、每段 1–3 位、0–255、不允许前导零与空格。 */
static inline bool pet_netcfg_ipv4(const char *text, uint8_t out[4])
{
    if(!text) return false;
    const char *p=text;
    for(unsigned part=0;part<4;part++) {
        if(part&&*p!='.') return false;
        if(part) p++;
        if(*p<'0'||*p>'9') return false;
        const char *start=p;
        unsigned value=0,digits=0;
        while(*p>='0'&&*p<='9') {
            if(digits==3) return false;
            value=value*10u+(unsigned)(*p-'0');
            if(value>255) return false;
            digits++;p++;
        }
        if(digits>1&&*start=='0') return false;
        out[part]=(uint8_t)value;
    }
    return *p==0;
}

/* 子网掩码必须是连续的 1，且不能为 0.0.0.0。 */
static inline bool pet_netcfg_netmask(const char *text)
{
    uint8_t bytes[4];
    if(!pet_netcfg_ipv4(text,bytes)) return false;
    uint32_t mask=(uint32_t)bytes[0]<<24|(uint32_t)bytes[1]<<16|(uint32_t)bytes[2]<<8|bytes[3];
    if(!mask) return false;
    uint32_t inverse=~mask;
    return (inverse&(inverse+1u))==0;
}

/* 同一子网判断：用于拒绝“网关不在本网段”的固定地址。 */
static inline bool pet_netcfg_same_subnet(const char *address,const char *mask,const char *gateway)
{
    uint8_t a[4],m[4],g[4];
    if(!pet_netcfg_ipv4(address,a)||!pet_netcfg_ipv4(mask,m)||!pet_netcfg_ipv4(gateway,g)) return false;
    for(unsigned i=0;i<4;i++) if((a[i]&m[i])!=(g[i]&m[i])) return false;
    return true;
}

/* 主机名用于 DHCP option 12：字母/数字/连字符，首尾不能是连字符。 */
static inline bool pet_netcfg_hostname(const char *text)
{
    if(!text) return false;
    size_t length=0;
    for(const char *p=text;*p;p++,length++) {
        char c=*p;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')) return false;
        if(length+1>PET_NETCFG_HOSTNAME_MAX) return false;
    }
    return length>0&&text[0]!='-'&&text[length-1]!='-';
}

static inline bool pet_netcfg_static(const char *mode)
{
    return mode&&!strcmp(mode,"static");
}

static inline bool pet_netcfg_mode(const char *mode)
{
    return !mode||!mode[0]||!strcmp(mode,"dhcp")||!strcmp(mode,"static");
}
