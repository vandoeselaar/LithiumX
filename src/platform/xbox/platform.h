#pragma once

#include <xbox.h>

extern ULONG platform_cpu_temp;
extern ULONG platform_mb_temp;
extern ULONG platform_tray_state;
extern UCHAR platform_temp_unit;

void platform_splash_set_status(const char *msg);
void platform_splash_overlay(void);