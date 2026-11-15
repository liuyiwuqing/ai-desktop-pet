set(audio_sources "")
if(CONFIG_PET_MODULE_AUDIO_ENABLED AND CONFIG_PET_AUDIO_FAKE)
    list(APPEND audio_sources "src/fake_audio.c")
endif()

idf_component_register(
    SRCS
        ${audio_sources}
        "src/audio_buffer.c"
        "src/audio_pcm.c"
        "src/climate_decode.c"
        "src/navigation.c"
        "src/fake_climate_sensor.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "contracts"
)
