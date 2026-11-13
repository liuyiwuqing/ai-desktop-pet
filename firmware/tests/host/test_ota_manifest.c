#include "svc_ota/manifest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned verifications;
static bool verify(void *ctx,const uint8_t *payload,size_t bytes,const uint8_t *sig,size_t count)
{(void)ctx;++verifications;return bytes>0&&payload[0]=='{'&&count==384&&sig[0]==1;}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    char envelope[4097];size_t bytes=fread(envelope,1,4096,file);fclose(file);
    pet_ota_identity_t identity={"nexus_pet","dodo-s3-lcd","none",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1,4};
    pet_ota_manifest_t manifest;
    assert(pet_ota_manifest_parse(envelope,bytes,&identity,9,verify,NULL,&manifest)==PET_OTA_VALID);
    assert(manifest.release_sequence==10&&manifest.image_size==1024&&!strcmp(manifest.image,"firmware.bin"));
    assert(pet_ota_manifest_parse(envelope,bytes,&identity,10,verify,NULL,&manifest)==PET_OTA_ROLLBACK);
    identity.model_sha256="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(pet_ota_manifest_parse(envelope,bytes,&identity,9,verify,NULL,&manifest)==PET_OTA_INCOMPATIBLE);
    envelope[20]='!';
    assert(pet_ota_manifest_parse(envelope,bytes,&identity,9,verify,NULL,&manifest)!=PET_OTA_VALID);
    assert(verifications>=3);puts("OTA manifest: signature boundary, version and model guards passed");
}
