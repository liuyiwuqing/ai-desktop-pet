#include "svc_ota/api.h"
#include "pet_build_identity.h"
static const pet_ota_identity_t identity={"nexus_pet","dodo-s3-lcd",PET_ID_MODEL,PET_ID_PARTITIONS,1,4};
static const char marker[]=PET_ID_MARKER;
const pet_ota_identity_t *pet_ota_identity(void){return &identity;}
const char *pet_ota_identity_marker(void){return marker;}
