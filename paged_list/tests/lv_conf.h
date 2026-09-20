#ifndef LV_CONF_H
#define LV_CONF_H

/* Test-only configuration: the smoke test creates objects but no display driver. */
#define LV_COLOR_DEPTH 32
#define LV_MEM_SIZE (512U * 1024U)

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif
