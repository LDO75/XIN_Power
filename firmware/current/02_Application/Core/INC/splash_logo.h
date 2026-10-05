#ifndef XIN_POWER_SPLASH_LOGO_H
#define XIN_POWER_SPLASH_LOGO_H

#include <stdint.h>

extern const uint8_t g_splash_logo_rle[];
extern const uint32_t g_splash_logo_rle_size;

void SplashLogo_Draw(void);

#endif
