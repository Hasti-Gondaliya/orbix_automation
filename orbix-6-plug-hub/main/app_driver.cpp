/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "soc/gpio_num.h"
#include <esp_log.h>
#include <bsp/esp-bsp.h>
#include "bsp/esp_bsp_devkit.h"
#include "support/CodeUtils.h"

#include <esp_matter.h>

#include <app_priv.h>
#include <button_gpio.h>
#include <iot_button.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";

static bool gpio_conflicts_with_plugs(gpio_num_t gpio)
{
    for (int i = 0; i < configure_plugs; i++) {
        if (plugin_unit_list[i].plug == gpio) {
            return true;
        }
    }
    return false;
}

static esp_err_t app_driver_update_gpio_value(gpio_num_t pin, bool value)
{
    esp_err_t err = ESP_OK;

    err = gpio_set_level(pin, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO level");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "GPIO pin : %d set to %d", pin, value);
    }
    return err;
}

esp_err_t app_driver_plugin_unit_init(const gpio_plug* plug)
{
    esp_err_t err = ESP_OK;

    gpio_reset_pin(plug->GPIO_PIN_VALUE);

    err = gpio_set_direction(plug->GPIO_PIN_VALUE, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set GPIO OUTPUT mode");
        return ESP_FAIL;
    }

    err = gpio_set_level(plug->GPIO_PIN_VALUE, 0);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Unable to set GPIO level");
    }
    return err;
}

// Return GPIO pin from plug-endpoint mapping list
gpio_num_t get_gpio(uint16_t endpoint_id)
{
    gpio_num_t gpio_pin = GPIO_NUM_NC;
    for (int i = 0; i < configure_plugs; i++) {
        if (plugin_unit_list[i].endpoint_id == endpoint_id) {
            gpio_pin = plugin_unit_list[i].plug;
        }
    }
    return gpio_pin;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            gpio_num_t gpio_pin = get_gpio(endpoint_id);
            if (gpio_pin != GPIO_NUM_NC) {
                err = app_driver_update_gpio_value(gpio_pin, val->val.b);
            } else {
                ESP_LOGE(TAG, "GPIO pin mapping for endpoint_id: %d not found", endpoint_id);
                return ESP_FAIL;
            }
        }
    }
    return err;
}

esp_err_t app_driver_restore_plug_states(void)
{
    for (int i = 0; i < configure_plugs; i++) {
        uint16_t endpoint_id = plugin_unit_list[i].endpoint_id;
        gpio_num_t gpio_pin = plugin_unit_list[i].plug;

        esp_matter_attr_val_t startup_val = esp_matter_nullable_enum8(nullable<uint8_t>());
        attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::StartUpOnOff::Id, &startup_val);

        attribute_t *attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
        if (!attr) {
            ESP_LOGE(TAG, "Failed to get OnOff attribute for plug %d", i + 1);
            continue;
        }

        esp_matter_attr_val_t val;
        esp_err_t get_err = attribute::get_val(attr, &val);
        if (get_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read plug %d OnOff from Matter: %d", i + 1, get_err);
            continue;
        }

        bool on = val.val.b;
        if (gpio_pin != GPIO_NUM_NC) {
            app_driver_update_gpio_value(gpio_pin, on);
        }

        ESP_LOGI(TAG, "Restored plug %d to %s", i + 1, on ? "ON" : "OFF");
    }

    ESP_LOGI(TAG, "Plug state restore complete");
    return ESP_OK;
}

void app_driver_reset_plug_states(void)
{
    for (int i = 0; i < configure_plugs; i++) {
        gpio_num_t gpio_pin = plugin_unit_list[i].plug;
        if (gpio_pin != GPIO_NUM_NC) {
            app_driver_update_gpio_value(gpio_pin, false);
        }

        uint16_t endpoint_id = plugin_unit_list[i].endpoint_id;
        esp_matter_attr_val_t val = esp_matter_bool(false);
        attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    }

    ESP_LOGI(TAG, "Plug states reset (all off)");
}

static esp_err_t app_driver_set_plug_state(int plug_index, bool on)
{
    if (plug_index < 0 || plug_index >= configure_plugs) {
        ESP_LOGE(TAG, "Invalid plug index %d", plug_index);
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t endpoint_id = plugin_unit_list[plug_index].endpoint_id;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    esp_matter_attr_val_t val = esp_matter_bool(on);
    esp_err_t err = attribute::update(endpoint_id, cluster_id, attribute_id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set plug %d to %d: %d", plug_index + 1, on, err);
        return err;
    }

    ESP_LOGI(TAG, "Plug %d set to %d", plug_index + 1, on);
    return ESP_OK;
}

/* Two single-way switches and two two-way pairs, four relays.
 * Pair 0: switches 3 and 4 -> plug 3. Pair 1: GPIO 33 and 32 -> plug 4.
 */
static constexpr int TWO_WAY_PAIR_COUNT = 2;

struct two_way_pair {
    int plug_index;
    gpio_num_t gpio[2];
    int count;
    bool ready;
};

static two_way_pair s_two_way[TWO_WAY_PAIR_COUNT];

/* Board relays that are not Matter endpoints. Held off so they cannot float. */
static const gpio_num_t s_unused_relay_gpio[] = {
    GPIO_NUM_16,
    GPIO_NUM_4,
};

static void app_driver_plug_button_press_down_cb(void *arg, void *data)
{
    app_driver_set_plug_state((int)(intptr_t)data, true);
}

static void app_driver_plug_button_press_up_cb(void *arg, void *data)
{
    app_driver_set_plug_state((int)(intptr_t)data, false);
}

static bool app_driver_switch_is_closed(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == CONFIG_PLUG_BUTTON_LEVEL;
}

static void app_driver_apply_two_way_state(int pair_index)
{
    if (pair_index < 0 || pair_index >= TWO_WAY_PAIR_COUNT) {
        return;
    }

    two_way_pair *pair = &s_two_way[pair_index];
    if (!pair->ready || pair->count < 2 || pair->plug_index < 0 || pair->plug_index >= configure_plugs) {
        return;
    }

    bool first_closed = app_driver_switch_is_closed(pair->gpio[0]);
    bool second_closed = app_driver_switch_is_closed(pair->gpio[1]);
    bool on = first_closed != second_closed;

    ESP_LOGI(TAG, "Two-way GPIO %d=%d GPIO %d=%d -> plug %d %s", pair->gpio[0], first_closed, pair->gpio[1],
             second_closed, pair->plug_index + 1, on ? "ON" : "OFF");
    app_driver_set_plug_state(pair->plug_index, on);
}

static void app_driver_two_way_changed_cb(void *arg, void *data)
{
    app_driver_apply_two_way_state((int)(intptr_t)data);
}

static esp_err_t app_driver_create_button(gpio_num_t gpio, button_cb_t down_cb, button_cb_t up_cb, void *usr_data)
{
    if (gpio_conflicts_with_plugs(gpio)) {
        ESP_LOGE(TAG, "Plug button GPIO %d conflicts with a plug output", gpio);
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_USER_BUTTON
    if (gpio == (gpio_num_t)CONFIG_USER_BUTTON_GPIO) {
        ESP_LOGE(TAG, "Plug button GPIO %d conflicts with factory reset button", gpio);
        return ESP_ERR_INVALID_STATE;
    }
#endif

    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = gpio,
        .active_level = CONFIG_PLUG_BUTTON_LEVEL,
    };

    button_handle_t handle = NULL;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle);
    if (err != ESP_OK || !handle) {
        ESP_LOGE(TAG, "Failed to create plug button on GPIO %d", gpio);
        return ESP_FAIL;
    }

    err = iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, down_cb, usr_data);
    err |= iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, up_cb, usr_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register plug button callbacks for GPIO %d", gpio);
        iot_button_delete(handle);
        return err;
    }

    return ESP_OK;
}

static esp_err_t app_driver_create_plug_button(gpio_num_t gpio, int plug_index)
{
    esp_err_t err = app_driver_create_button(gpio, app_driver_plug_button_press_down_cb,
                                             app_driver_plug_button_press_up_cb, (void *)(intptr_t)plug_index);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Plug %d single-way switch initialized on GPIO %d", plug_index + 1, gpio);
    return ESP_OK;
}

static esp_err_t app_driver_create_two_way_switch(int pair_index, gpio_num_t gpio)
{
    if (pair_index < 0 || pair_index >= TWO_WAY_PAIR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    two_way_pair *pair = &s_two_way[pair_index];
    if (pair->count >= 2) {
        ESP_LOGE(TAG, "Two-way pair %d already has two switches", pair_index + 1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = app_driver_create_button(gpio, app_driver_two_way_changed_cb, app_driver_two_way_changed_cb,
                                             (void *)(intptr_t)pair_index);
    if (err != ESP_OK) {
        return err;
    }

    pair->gpio[pair->count++] = gpio;
    ESP_LOGI(TAG, "Two-way switch for plug %d initialized on GPIO %d", pair->plug_index + 1, gpio);
    return ESP_OK;
}

static esp_err_t app_driver_init_two_way_pair(int pair_index, int plug_index, gpio_num_t gpio_a, gpio_num_t gpio_b)
{
    s_two_way[pair_index].plug_index = plug_index;
    s_two_way[pair_index].count = 0;
    s_two_way[pair_index].ready = false;

    esp_err_t err_a = app_driver_create_two_way_switch(pair_index, gpio_a);
    esp_err_t err_b = app_driver_create_two_way_switch(pair_index, gpio_b);
    if (err_a == ESP_OK && err_b == ESP_OK) {
        s_two_way[pair_index].ready = true;
        app_driver_apply_two_way_state(pair_index);
        return ESP_OK;
    }

    return err_a != ESP_OK ? err_a : err_b;
}

void app_driver_hold_unused_relays_off(void)
{
    for (size_t i = 0; i < sizeof(s_unused_relay_gpio) / sizeof(s_unused_relay_gpio[0]); i++) {
        gpio_num_t pin = s_unused_relay_gpio[i];
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
        ESP_LOGI(TAG, "Unused relay GPIO %d held off", pin);
    }
}

#define CREATE_PLUG_BUTTON(plug_id) \
    app_driver_create_plug_button((gpio_num_t)CONFIG_GPIO_PLUG_BUTTON_##plug_id, plug_id - 1)

esp_err_t app_driver_plug_buttons_init(void)
{
    esp_err_t err = ESP_OK;

#ifdef CONFIG_GPIO_PLUG_BUTTON_1
    err |= CREATE_PLUG_BUTTON(1);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_2
    err |= CREATE_PLUG_BUTTON(2);
#endif
#if defined(CONFIG_GPIO_PLUG_BUTTON_3) && defined(CONFIG_GPIO_PLUG_BUTTON_4)
    err |= app_driver_init_two_way_pair(0, 2, (gpio_num_t)CONFIG_GPIO_PLUG_BUTTON_3,
                                        (gpio_num_t)CONFIG_GPIO_PLUG_BUTTON_4);
#endif
#if defined(CONFIG_GPIO_PLUG_BUTTON_5) && defined(CONFIG_GPIO_PLUG_BUTTON_6)
    err |= app_driver_init_two_way_pair(1, 3, (gpio_num_t)CONFIG_GPIO_PLUG_BUTTON_5,
                                        (gpio_num_t)CONFIG_GPIO_PLUG_BUTTON_6);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_7
    err |= CREATE_PLUG_BUTTON(7);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_8
    err |= CREATE_PLUG_BUTTON(8);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_9
    err |= CREATE_PLUG_BUTTON(9);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_10
    err |= CREATE_PLUG_BUTTON(10);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_11
    err |= CREATE_PLUG_BUTTON(11);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_12
    err |= CREATE_PLUG_BUTTON(12);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_13
    err |= CREATE_PLUG_BUTTON(13);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_14
    err |= CREATE_PLUG_BUTTON(14);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_15
    err |= CREATE_PLUG_BUTTON(15);
#endif
#ifdef CONFIG_GPIO_PLUG_BUTTON_16
    err |= CREATE_PLUG_BUTTON(16);
#endif

    return err;
}

app_driver_handle_t app_driver_button_init(gpio_num_t * reset_gpio)
{
    VerifyOrReturnValue((reset_gpio), (app_driver_handle_t)NULL, ESP_LOGE(TAG, "reset_gpio cannot be NULL"));
#ifdef CONFIG_USER_BUTTON
    *reset_gpio = (gpio_num_t)CONFIG_USER_BUTTON_GPIO;
#elif CONFIG_BSP_BUTTONS_NUM >= 1
    *reset_gpio = (gpio_num_t)BSP_BUTTON_1_IO;
#else
    *reset_gpio = gpio_num_t::GPIO_NUM_NC;
    return (app_driver_handle_t)NULL;
#endif
    ESP_LOGI(TAG, "Initializing factory reset button on GPIO %d ...", (int)*reset_gpio);

    if (gpio_conflicts_with_plugs(*reset_gpio)) {
        ESP_LOGE(TAG, "Factory reset button GPIO %d is already configured for a plug", *reset_gpio);
        *reset_gpio = gpio_num_t::GPIO_NUM_NC;
        return (app_driver_handle_t)NULL;
    }

    app_driver_handle_t reset_handle = NULL;
#ifdef CONFIG_USER_BUTTON
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = CONFIG_USER_BUTTON_GPIO,
        .active_level = CONFIG_USER_BUTTON_LEVEL,
    };

    button_handle_t handle = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create factory reset button");
        return NULL;
    }
    reset_handle = (app_driver_handle_t)handle;
#else
    button_handle_t bsp_buttons[BSP_BUTTON_NUM];
    int btn_cnt = 0;
    bsp_iot_button_create(bsp_buttons, &btn_cnt, BSP_BUTTON_NUM);
    if (btn_cnt >= 1) {
        reset_handle = (app_driver_handle_t)bsp_buttons[0];
    } else {
        ESP_LOGE(TAG, "bsp_iot_button_create() didn't return a usable button count: %d", btn_cnt);
    }
#endif

    if (!reset_handle) {
        *reset_gpio = gpio_num_t::GPIO_NUM_NC;
    }
    return reset_handle;
}
