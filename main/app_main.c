// Copyright 2016-2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#undef false
#undef true
#include "i_system.h"

#include "i80_lcd.h"

void doom_task(void *pvParameters)
{
    char const *argv[] = {
        "doom", "-cout", "ICWEFDA"
    };
    doom_main(sizeof(argv)/sizeof(argv[0]), argv);
}

void app_main()
{
    i80_lcd_init(); // early init to catch errors
    xTaskCreatePinnedToCore(&doom_task, "doom", 22480, NULL, 5, NULL, 0);
}
