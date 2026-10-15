#pragma once
#include "config.h"

#ifndef APP_BUILD_TS
#define APP_BUILD_TS "unknown"
#endif

#ifndef APP_GIT_HASH
#define APP_GIT_HASH "nogit"
#endif

#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "unknown"
#endif

#ifndef APP_BUILD_TIME
#define APP_BUILD_TIME "unknown"
#endif

void print_build_info(void);
