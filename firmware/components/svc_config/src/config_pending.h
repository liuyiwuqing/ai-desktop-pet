#pragma once
#include "svc_config/api.h"

/* A queued local change owns the next CAS until it is saved or its enqueue fails. */
esp_err_t pet_config_reserve(uint32_t expected_revision,uint32_t *lease);
esp_err_t pet_config_apply_reserved(const pet_config_t *config,uint32_t expected_revision,uint32_t lease);
void pet_config_cancel_reservation(uint32_t lease);
