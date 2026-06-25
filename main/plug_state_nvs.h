/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

/** Initialize plug state NVS storage.
 *
 * Must be called after nvs_flash_init().
 */
esp_err_t plug_state_nvs_init(void);

/** Read saved on/off state for a plug.
 *
 * @param[in] plug_index Zero-based plug index.
 * @param[out] on Saved state. Defaults to false when no value is stored.
 */
esp_err_t plug_state_nvs_get(int plug_index, bool *on);

/** Persist on/off state for a plug. */
esp_err_t plug_state_nvs_set(int plug_index, bool on);
