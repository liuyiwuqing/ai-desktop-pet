#include "svc_config/provision.h"
#include "sdkconfig.h"
#include <stdbool.h>
static pet_maintenance_handler_t command_handler=pet_config_command;
esp_err_t pet_config_console_set_handler(pet_maintenance_handler_t handler)
{if (!handler) return ESP_ERR_INVALID_ARG;command_handler=handler;return ESP_OK;}
#if CONFIG_PET_PROVISION_CONSOLE_ENABLED
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static void console(void *arg)
{
    char *line=arg;
    char *response=line+PET_PROVISION_JSON_MAX+129;
    size_t used=0; bool dropping=false;
    for (;;) {
        int ch=getchar();
        if (ch==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (ch=='\r') continue;
        if (ch=='\n') {
            if (dropping) puts("PET_RESULT {\"ok\":false,\"code\":260}");
            else if (used) { command_handler(line,used,response,PET_MAINTENANCE_RESPONSE_MAX); printf("PET_RESULT %s\n",response); }
            volatile char *p=line; for (size_t i=0;i<used;++i) p[i]=0;
            used=0; dropping=false;
        } else if (!dropping && used<PET_PROVISION_JSON_MAX+128) line[used++]=(char)ch;
        else dropping=true;
    }
}
esp_err_t pet_config_console_start(void)
{
    char *line=calloc(1,PET_PROVISION_JSON_MAX+129+PET_MAINTENANCE_RESPONSE_MAX);
    if (!line) return ESP_ERR_NO_MEM;
    if (xTaskCreate(console,"pet_provision",8192,line,1,NULL)!=pdPASS) { free(line); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
#else
esp_err_t pet_config_console_start(void) { return ESP_OK; }
#endif
