#include "svc_ota/manifest.h"
#include "pet/json_guard.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>
static bool fields(const cJSON *root,const char *const *names,unsigned count)
{
    if(!cJSON_IsObject(root)||cJSON_GetArraySize(root)!=(int)count)return false;
    unsigned seen=0;
    for(const cJSON *p=root->child;p;p=p->next) {
        unsigned i=0;while(i<count&&strcmp(p->string,names[i]))++i;
        if(i==count||(seen&(1u<<i)))return false;
        seen|=1u<<i;
    }
    return true;
}
static const cJSON *get(const cJSON *root,const char *key){return cJSON_GetObjectItemCaseSensitive(root,key);}
static const char *string(const cJSON *root,const char *key)
{const cJSON *p=get(root,key);return cJSON_IsString(p)?p->valuestring:NULL;}
static bool uint32(const cJSON *p,uint32_t *out)
{
    if(!cJSON_IsNumber(p)||!isfinite(p->valuedouble)||p->valuedouble<0||p->valuedouble>UINT32_MAX||floor(p->valuedouble)!=p->valuedouble)return false;
    *out=(uint32_t)p->valuedouble;return true;
}
static cJSON *parse(const char *text,size_t bytes,unsigned depth)
{
    if(!pet_json_bounded(text,bytes,depth))return NULL;
    const char *end=NULL;cJSON *root=cJSON_ParseWithLengthOpts(text,bytes,&end,false);
    while(root&&end<text+bytes&&(*end==' '||*end=='\r'||*end=='\n'||*end=='\t'))++end;
    if(!root||end!=text+bytes){cJSON_Delete(root);return NULL;}return root;
}
static int value(unsigned char c)
{return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;}
static bool decode(const char *text,uint8_t *out,size_t cap,size_t *bytes)
{
    if(!text)return false;
    size_t length=strlen(text),used=0;if(!length||length%4||length>4*((cap+2)/3))return false;
    for(size_t i=0;i<length;i+=4) {
        int a=value(text[i]),b=value(text[i+1]),c=value(text[i+2]),d=value(text[i+3]);
        bool p2=text[i+2]=='=',p3=text[i+3]=='=';
        if(a<0||b<0||(!p2&&c<0)||(!p3&&d<0)||(p2&&!p3)||((p2||p3)&&i+4!=length)||
            (p2&&(b&15))||(!p2&&p3&&(c&3)))return false;
        size_t n=p2?1:p3?2:3;if(used+n>cap)return false;
        out[used++]=(uint8_t)((a<<2)|(b>>4));
        if(n>1)out[used++]=(uint8_t)((b<<4)|(c>>2));
        if(n>2)out[used++]=(uint8_t)((c<<6)|d);
    }
    *bytes=used;return true;
}
static bool hex(const char *text)
{if(!text||strlen(text)!=64)return false;for(unsigned i=0;i<64;++i)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return false;return true;}
pet_ota_validation_t pet_ota_manifest_parse(const char *json,size_t bytes,const pet_ota_identity_t *identity,
    uint32_t minimum,pet_ota_verify_t verify,void *ctx,pet_ota_manifest_t *out)
{
    if(!identity||!identity->project||!identity->board||!identity->model_sha256||!identity->partition_sha256||!verify||!out||bytes>PET_OTA_ENVELOPE_MAX)return PET_OTA_INVALID;
    static const char *const envelope_keys[]={"payload","signature"};
    static const char *const payload_keys[]={"format","project","board","version","modelSha256","partitionSha256",
        "imageSha256","image","imageSize","releaseSequence","configSchema","growthSchema"};
    pet_ota_validation_t result=PET_OTA_INVALID;cJSON *envelope=parse(json,bytes,1),*payload=NULL;
    uint8_t raw[2048],signature[384];size_t raw_bytes=0,sig_bytes=0;
    if(!fields(envelope,envelope_keys,2)||!decode(string(envelope,"payload"),raw,sizeof(raw),&raw_bytes)||
        !decode(string(envelope,"signature"),signature,sizeof(signature),&sig_bytes)||sig_bytes!=384)goto done;
    result=PET_OTA_BAD_SIGNATURE;if(!verify(ctx,raw,raw_bytes,signature,sig_bytes))goto done;
    result=PET_OTA_INVALID;payload=parse((const char *)raw,raw_bytes,1);
    if(!fields(payload,payload_keys,12))goto done;
    uint32_t format,config,growth;pet_ota_manifest_t candidate={0};
    const char *project=string(payload,"project"),*board=string(payload,"board"),*version=string(payload,"version"),
        *model=string(payload,"modelSha256"),*partitions=string(payload,"partitionSha256"),*hash=string(payload,"imageSha256"),*image=string(payload,"image");
    if(!uint32(get(payload,"format"),&format)||format!=1||!project||!board||!version||!model||!hex(partitions)||!hex(hash)||
        !image||strcmp(image,"firmware.bin")||!uint32(get(payload,"imageSize"),&candidate.image_size)||candidate.image_size<288||candidate.image_size>0x400000||
        !uint32(get(payload,"releaseSequence"),&candidate.release_sequence)||!uint32(get(payload,"configSchema"),&config)||!uint32(get(payload,"growthSchema"),&growth)||
        !version[0]||strlen(version)>32)goto done;
    for(const char *p=version;*p;++p)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='.'||*p=='-'||*p=='_'))goto done;
    result=PET_OTA_INCOMPATIBLE;
    if(strcmp(project,identity->project)||strcmp(board,identity->board)||strcmp(model,identity->model_sha256)||
        strcmp(partitions,identity->partition_sha256)||config!=identity->config_schema||growth!=identity->growth_schema)goto done;
    result=PET_OTA_ROLLBACK;if(candidate.release_sequence<=minimum)goto done;
    strcpy(candidate.version,version);strcpy(candidate.image,image);strcpy(candidate.image_sha256,hash);*out=candidate;result=PET_OTA_VALID;
done:
    cJSON_Delete(payload);cJSON_Delete(envelope);return result;
}
