#include "ota_manager.h"
#include "ota_wifi.h"

void ota_manager_start(void)
{
    ota_wifi_init();
}