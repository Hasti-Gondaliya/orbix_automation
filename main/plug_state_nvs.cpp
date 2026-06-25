/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "plug_state_nvs.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>

static const char *TAG = "plug_state_nvs";
static const char *NVS_NAMESPACE = "orbix_plug";

static nvs_handle_t s_nvs_handle = 0;
static bool s_initialized = false;

static esp_err_t plug_state_nvs_key(int plug_index, char *key, size_t key_size)
{
    int written = snprintf(key, key_size, "plug_%d", plug_index);
    if (written < 0 || (size_t)written >= key_size) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t plug_state_nvs_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Plug state NVS initialized");
    return ESP_OK;
}

esp_err_t plug_state_nvs_get(int plug_index, bool *on)
{
    if (!on) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (plug_index < 0 || plug_index >= CONFIG_NUM_VIRTUAL_PLUGS) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    esp_err_t err = plug_state_nvs_key(plug_index, key, sizeof(key));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = nvs_get_u8(s_nvs_handle, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *on = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read %s: %s", key, esp_err_to_name(err));
        return err;
    }

    *on = value != 0;
    return ESP_OK;
}

esp_err_t plug_state_nvs_set(int plug_index, bool on)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (plug_index < 0 || plug_index >= CONFIG_NUM_VIRTUAL_PLUGS) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    esp_err_t err = plug_state_nvs_key(plug_index, key, sizeof(key));
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(s_nvs_handle, key, on ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit plug state: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGD(TAG, "Saved plug %d state: %d", plug_index + 1, on);
    return ESP_OK;
}
