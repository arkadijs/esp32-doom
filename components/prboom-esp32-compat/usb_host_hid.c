/* ESP32-S3 USB HID host glue for keyboards, mice, and the Sandio O2 mouse. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"

#include "d_main.h"
#include "doomstat.h"
#include "i_usb.h"

#define INPUT_QUEUE_LENGTH 32
#define MAX_HID_INTERFACES 6
#define MAX_REPORTS 4
/* Doom's busy task owns core 0. Run the USB host stack, and with it the USB
   interrupt (allocated on the core that installs the host library), on core 1
   at a priority above Doom so enumeration control transfers do not time out. */
#define USB_TASK_PRIORITY 7
#ifdef CONFIG_FREERTOS_UNICORE
#define USB_TASK_CORE 0
#else
#define USB_TASK_CORE 1
#endif
#define REPORT_BUFFER_SIZE 64

#define HID_USAGE_PAGE_DESKTOP 0x01
#define HID_USAGE_MOUSE 0x02
#define HID_USAGE_KEYBOARD 0x06
#define HID_USAGE_MULTI_AXIS 0x08

typedef struct {
    uint8_t id;
    uint16_t usage_page;
    uint16_t usage;
} report_info_t;

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_dev_params_t params;
    bool sandio;
    size_t report_count;
    report_info_t reports[MAX_REPORTS];
} interface_t;

static const char *TAG = "usb";
static QueueHandle_t input_queue;
static StaticQueue_t input_queue_storage;
static uint8_t input_queue_items[INPUT_QUEUE_LENGTH * sizeof(event_t)];
static interface_t interfaces[MAX_HID_INTERFACES];
static unsigned sandio_interfaces;
static QueueHandle_t recover_queue;
#define RESTART_DELAY_MS 10

/* Mouse reports arrive far faster than the game polls; accumulate motion and
   deliver one event per poll so they cannot flood the queue and push out key
   events (a dropped key-up leaves the key stuck). */
static portMUX_TYPE mouse_lock = portMUX_INITIALIZER_UNLOCKED;
static int mouse_dx, mouse_dy;
static uint8_t mouse_buttons;
static bool mouse_pending;

void I_USB_AccumulateMouse(uint8_t buttons, int dx, int dy)
{
    taskENTER_CRITICAL(&mouse_lock);
    mouse_dx += dx;
    mouse_dy += dy;
    mouse_buttons = buttons;
    mouse_pending = true;
    taskEXIT_CRITICAL(&mouse_lock);
}

bool I_USB_PostEvent(const event_t *event)
{
    bool ok = input_queue && xQueueSend(input_queue, event, 0) == pdTRUE;
    return ok;
}

void I_USB_Poll(void)
{
    event_t event;
    /* PrBoom deliberately discards input during its first three game tics.
       Leave early connection events queued so Sandio's always-run key and
       keys held during startup are not silently lost. */
    if (gametic < 3) {
        return;
    }
    while (input_queue && xQueueReceive(input_queue, &event, 0) == pdTRUE) {
        D_PostEvent(&event);
    }

    I_MouseTick();

    taskENTER_CRITICAL(&mouse_lock);
    event_t mouse = {
        .type = ev_mouse,
        .data1 = mouse_buttons,
        .data2 = mouse_dx,
        .data3 = mouse_dy,
    };
    bool pending = mouse_pending;
    mouse_dx = mouse_dy = 0;
    mouse_pending = false;
    taskEXIT_CRITICAL(&mouse_lock);
    if (pending) {
        D_PostEvent(&mouse);
    }
}

static interface_t *allocate_interface(hid_host_device_handle_t handle)
{
    for (size_t i = 0; i < MAX_HID_INTERFACES; ++i) {
        if (!interfaces[i].handle) {
            memset(&interfaces[i], 0, sizeof(interfaces[i]));
            interfaces[i].handle = handle;
            return &interfaces[i];
        }
    }
    return NULL;
}

static uint32_t item_value(const uint8_t *data, size_t size)
{
    uint32_t value = 0;
    for (size_t i = 0; i < size && i < sizeof(value); ++i) {
        value |= (uint32_t)data[i] << (8 * i);
    }
    return value;
}

/* HID report-descriptor parser, records each top-level collection and report ID. */
static size_t parse_report_descriptor(report_info_t *reports, size_t capacity,
                                      const uint8_t *desc, size_t len)
{
    size_t count = 0;
    unsigned depth = 0;
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    uint8_t report_id = 0;

    memset(reports, 0, capacity * sizeof(*reports));
    while (len && count < capacity) {
        uint8_t prefix = *desc++;
        --len;
        if (prefix == 0xfe) {
            if (len < 2 || len < (size_t)desc[0] + 2) {
                break;
            }
            size_t long_size = desc[0];
            desc += long_size + 2;
            len -= long_size + 2;
            continue;
        }

        size_t size = prefix & 3;
        if (size == 3) {
            size = 4;
        }
        if (len < size) {
            break;
        }
        uint8_t type = (prefix >> 2) & 3;
        uint8_t tag = prefix >> 4;
        uint32_t value = item_value(desc, size);

        if (type == 1 && tag == 0 && depth == 0) {       /* Usage Page */
            usage_page = value;
        } else if (type == 1 && tag == 8) {              /* Report ID */
            report_id = value;
            if (depth) {
                reports[count].id = report_id;
            }
        } else if (type == 2 && tag == 0 && depth == 0) { /* Usage */
            usage = value;
        } else if (type == 0 && tag == 10) {             /* Collection */
            if (depth++ == 0) {
                reports[count].id = report_id;
                reports[count].usage_page = usage_page;
                reports[count].usage = usage;
            }
        } else if (type == 0 && tag == 12 && depth) {    /* End Collection */
            if (--depth == 0) {
                ++count;
                usage = 0;
            }
        }
        desc += size;
        len -= size;
    }
    return count;
}

static const report_info_t *find_report(interface_t *itf,
                                        const uint8_t **data, size_t *len)
{
    if (itf->report_count == 1 && itf->reports[0].id == 0) {
        return &itf->reports[0];
    }
    if (!*len) {
        return NULL;
    }

    uint8_t id = **data;
    ++*data;
    --*len;
    for (size_t i = 0; i < itf->report_count; ++i) {
        if (itf->reports[i].id == id) {
            return &itf->reports[i];
        }
    }
    return NULL;
}

static void process_report(interface_t *itf, const uint8_t *data, size_t len)
{
    if (itf->params.proto == HID_PROTOCOL_KEYBOARD) {
        I_KeyboardReport(data, len);
        return;
    }
    if (itf->params.proto == HID_PROTOCOL_MOUSE) {
        /* Receivers such as Logitech's stay in report protocol after
           SET_PROTOCOL(boot): 8 bytes, report ID 2, then 12-bit axes. */
        if (!itf->sandio && len == 8 && data[0] == 2) {
            I_MouseReport12(data + 1, len - 1);
            return;
        }
        I_MouseReport(data, len);
        return;
    }

    const uint8_t *raw_data = data;
    size_t raw_len = len;
    const report_info_t *info = find_report(itf, &data, &len);
    if (info && info->usage_page == HID_USAGE_PAGE_DESKTOP) {
        if (info->usage == HID_USAGE_KEYBOARD) {
            I_KeyboardReport(data, len);
        } else if (info->usage == HID_USAGE_MOUSE) {
            I_MouseReport(data, len);
        } else if (info->usage == HID_USAGE_MULTI_AXIS && itf->sandio) {
            I_SandioReport(data, len);
        }
    } else if (itf->sandio) {
        /* The O2 controller payload is seven bytes. Keep this fallback for
           firmware revisions whose descriptor does not parse as expected. */
        if (raw_len == 8) {
            ++raw_data;
            --raw_len;
        }
        I_SandioReport(raw_data, raw_len);
    }
}

static void interface_callback(hid_host_device_handle_t handle,
                               hid_host_interface_event_t event, void *arg)
{
    interface_t *itf = arg;
    if (!itf || itf->handle != handle) {
        return;
    }

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[REPORT_BUFFER_SIZE];
        size_t len = 0;
        esp_err_t err = hid_host_device_get_raw_input_report_data(
            handle, data, sizeof(data), &len);
        if (err == ESP_OK) {
            process_report(itf, data, len);
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID device %u interface %u disconnected",
                 itf->params.addr, itf->params.iface_num);
        if (itf->params.proto == HID_PROTOCOL_KEYBOARD) {
            I_KeyboardDisconnected();
        } else if (itf->params.proto == HID_PROTOCOL_MOUSE) {
            I_MouseDisconnected();
        }
        if (itf->sandio && sandio_interfaces && --sandio_interfaces == 0) {
            I_SandioDisconnected();
        }
        hid_host_device_close(handle);
        memset(itf, 0, sizeof(*itf));
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        /* The driver does not resubmit after an error. Restart the interface
           from a separate task, it cannot be re-entered from this callback. */
        if (recover_queue) {
            xQueueSend(recover_queue, &handle, 0);
        }
        break;
    default:
        break;
    }
}

static void device_callback(hid_host_device_handle_t handle,
                            hid_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
        return;
    }

    interface_t *itf = allocate_interface(handle);
    if (!itf) {
        ESP_LOGE(TAG, "Too many HID interfaces");
        return;
    }

    if (hid_host_device_get_params(handle, &itf->params) != ESP_OK) {
        memset(itf, 0, sizeof(*itf));
        return;
    }

    const hid_host_device_config_t config = {
        .callback = interface_callback,
        .callback_arg = itf,
    };
    esp_err_t err = hid_host_device_open(handle, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open HID interface: %s", esp_err_to_name(err));
        memset(itf, 0, sizeof(*itf));
        return;
    }

    hid_host_dev_info_t device_info;
    if (hid_host_get_device_info(handle, &device_info) != ESP_OK) {
        hid_host_device_close(handle);
        memset(itf, 0, sizeof(*itf));
        return;
    }
    itf->sandio = device_info.VID == 0x19ca && device_info.PID == 0x0001;

    /* Control transfers on a flaky low-speed link can complete after the
       device is closed, and the HID driver then signals a freed semaphore.
       Boot-class interfaces need none: their report format is known (devices
       power up in report protocol, which keyboards and the Logitech mouse
       share with boot format), and the O2's layout is hard-coded (it also
       never answers these requests, each stalling for the 5 s timeout). */
    if (!itf->sandio && itf->params.sub_class != HID_SUBCLASS_BOOT_INTERFACE) {
        size_t desc_len = 0;
        const uint8_t *desc = hid_host_get_report_descriptor(handle, &desc_len);
        if (desc) {
            itf->report_count = parse_report_descriptor(
                itf->reports, MAX_REPORTS, desc, desc_len);
        }
    }

    ESP_LOGI(TAG, "HID %04x:%04x device %u interface %u protocol %u%s",
             device_info.VID, device_info.PID, itf->params.addr,
             itf->params.iface_num, itf->params.proto,
             itf->sandio ? " (Sandio O2)" : "");
    err = hid_host_device_start(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start HID interface: %s", esp_err_to_name(err));
        hid_host_device_close(handle);
        memset(itf, 0, sizeof(*itf));
    } else if (itf->sandio && sandio_interfaces++ == 0) {
        I_SandioConnected();
    }
}

/* Restarts interfaces whose interrupt transfer failed. The bus itself is never
   reset: that disrupts the game more than a short gap in input does. */
static void recover_task(void *arg)
{
    (void)arg;
    hid_host_device_handle_t handle;
    while (xQueueReceive(recover_queue, &handle, portMAX_DELAY) == pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
        for (size_t i = 0; i < MAX_HID_INTERFACES; ++i) {
            if (interfaces[i].handle == handle) {
                hid_host_device_stop(handle);
                hid_host_device_start(handle);
            }
        }
    }
}

static void usb_host_task(void *arg)
{
    (void)arg;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = USB_TASK_PRIORITY,
        .stack_size = 3072,
        .core_id = USB_TASK_CORE,
        .callback = device_callback,
        .callback_arg = NULL,
    };
    err = hid_host_install(&hid_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID host install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    xTaskCreatePinnedToCore(recover_task, "usb_recover", 2560, NULL,
                            USB_TASK_PRIORITY - 1, NULL, USB_TASK_CORE);
    ESP_LOGI(TAG, "USB HID host ready");
    while (true) {
        uint32_t event_flags;
        err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "USB host event loop stopped: %s", esp_err_to_name(err));
            break;
        }
    }
    vTaskDelete(NULL);
}

void usb_init(void)
{
    /* The stack logs every transfer error; UART output only adds stalls. */
    esp_log_level_set("hid-host", ESP_LOG_NONE);
    esp_log_level_set("USB HOST", ESP_LOG_NONE);

    input_queue = xQueueCreateStatic(INPUT_QUEUE_LENGTH, sizeof(event_t),
                                     input_queue_items, &input_queue_storage);
    if (!input_queue) {
        ESP_LOGE(TAG, "Cannot create input queue");
        return;
    }
    recover_queue = xQueueCreate(8, sizeof(hid_host_device_handle_t));
    if (xTaskCreatePinnedToCore(usb_host_task, "usb_host", 3072, NULL,
                                USB_TASK_PRIORITY,
                                NULL, USB_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create USB host task");
    }
}
