#pragma once
void host_audio_diagnostic_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) host_audio_diagnostic_log(__VA_ARGS__)
#define ESP_LOGW(...) host_audio_diagnostic_log(__VA_ARGS__)
