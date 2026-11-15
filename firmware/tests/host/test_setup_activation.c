#include "setup_activation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static esp_err_t parse(const char *json,pet_setup_activation_result_t *result)
{return pet_setup_activation_parse(json,strlen(json),result);}

int main(void)
{
    pet_setup_activation_result_t r;
    assert(parse("{\"activation\":{\"code\":\"123456\",\"challenge\":\"bounded-test\"}}",&r)==ESP_OK);
    assert(r.waiting&&r.has_challenge&&!strcmp(r.code,"123456")&&!r.token[0]);
    assert(parse("{\"activation\":{\"code\":\"<script>\"}}",&r)==ESP_ERR_INVALID_ARG);
    assert(parse("{\"firmware\":{\"url\":\"https://example.test/untrusted.bin\"}}",&r)==ESP_ERR_INVALID_ARG);
    assert(parse("{\"websocket\":{\"url\":\"wss://example.test/v1/\",\"token\":\"test-token\"},\"firmware\":{\"force\":1}}",&r)==ESP_OK);
    assert(!r.waiting&&!strcmp(r.uri,"wss://example.test/v1/")&&!strcmp(r.token,"test-token"));
    assert(parse("{\"websocket\":{\"url\":\"ws://example.test/\",\"token\":\"test-token\"}}",&r)==ESP_ERR_INVALID_ARG);
    assert(parse("{\"websocket\":{\"url\":\"wss://user@example.test/\",\"token\":\"test-token\"}}",&r)==ESP_ERR_INVALID_ARG);
    assert(parse("{\"websocket\":{\"url\":\"wss://example.test/\",\"token\":\"\"}}",&r)==ESP_ERR_INVALID_ARG);
    assert(!r.token[0]&&!r.uri[0]);
    puts("Setup activation: bounded binding code, verified WSS credentials and ignored firmware payload passed");
}
