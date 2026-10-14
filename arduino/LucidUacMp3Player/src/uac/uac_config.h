#pragma once
#include "sdkconfig.h"

// These are normally supplied by Espressif's CMake package manager.
#define USB_HOST_UAC_VER_MAJOR 1
#define USB_HOST_UAC_VER_MINOR 5
#define USB_HOST_UAC_VER_PATCH 0

// Arduino does not run the driver's Kconfig. Supply its upstream defaults here.
#ifndef CONFIG_UAC_FREQ_NUM_MAX
#define CONFIG_UAC_FREQ_NUM_MAX 4
#endif
#ifndef CONFIG_UAC_DEV_ADDR_LIST_MAX
#define CONFIG_UAC_DEV_ADDR_LIST_MAX 6
#endif
#ifndef CONFIG_UAC_NUM_ISOC_URBS
#define CONFIG_UAC_NUM_ISOC_URBS 3
#endif
#ifndef CONFIG_UAC_NUM_PACKETS_PER_URB
#define CONFIG_UAC_NUM_PACKETS_PER_URB 3
#endif
#ifndef CONFIG_UAC_RINGBUF_SAFE_DELETE_WAITING_MS
#define CONFIG_UAC_RINGBUF_SAFE_DELETE_WAITING_MS 50
#endif
