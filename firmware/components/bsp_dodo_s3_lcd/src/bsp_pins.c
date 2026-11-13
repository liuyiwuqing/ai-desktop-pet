#include "pet/hal/pins.h"
#include "board_generated.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
bool pet_bsp_gpio_available(int gpio)
{
    if(!GPIO_IS_VALID_OUTPUT_GPIO(gpio)||gpio==0||gpio==3||gpio==19||gpio==20||
        (gpio>=26&&gpio<=37)||gpio==45||gpio==46)return false;
    const int used[]={PET_BOARD_LCD_MOSI,PET_BOARD_LCD_SCLK,PET_BOARD_LCD_CS,PET_BOARD_LCD_DC,
        PET_BOARD_LCD_RESET,PET_BOARD_LCD_BACKLIGHT,PET_BOARD_BOOT_GPIO,PET_BOARD_DHT11_GPIO,
        PET_BOARD_MIC_BCLK,PET_BOARD_MIC_WS,PET_BOARD_MIC_DIN,PET_BOARD_SPEAKER_BCLK,
        PET_BOARD_SPEAKER_WS,PET_BOARD_SPEAKER_DOUT,
#ifdef CONFIG_PET_CLIMATE_DHT11_GPIO
        CONFIG_PET_CLIMATE_DHT11_GPIO,
#endif
    };
    for(unsigned i=0;i<sizeof(used)/sizeof(used[0]);++i)if(used[i]==gpio)return false;
    return true;
}
