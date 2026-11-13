set(audio_sources "")
if(CONFIG_PET_MODULE_AUDIO_ENABLED)
    list(APPEND audio_sources "src/audio_loopback.c")
    if(CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT)
        list(APPEND audio_sources "src/audio_diagnostic.c")
    endif()
endif()

idf_component_register(
    SRCS ${audio_sources}
    INCLUDE_DIRS "include"
    REQUIRES "contracts" "pet_hal"
    PRIV_REQUIRES "heap"
)
