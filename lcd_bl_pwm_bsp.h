#ifndef LCD_BL_PWM_BSP_H
#define LCD_BL_PWM_BSP_H

#include <stdint.h>

#define LCD_PWM_MODE_255 255 // Full brightness

#ifdef __cplusplus
extern "C"
{
#endif

  void lcd_bl_pwm_bsp_init(uint16_t duty);
  void setUpdutySubdivide(uint16_t duty);

#ifdef __cplusplus
}
#endif

#endif
