#ifndef _UI_DINOGAMEPAGE_H
#define _UI_DINOGAMEPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../ui.h"
// 声明图像资源
extern const lv_image_dsc_t ui_img_dino_runner_png;
extern const lv_image_dsc_t ui_img_dino_icon_png;
extern const lv_image_dsc_t ui_img_sun_png;
extern const lv_image_dsc_t ui_img_clouds_png;

void ui_DinoGamePage_init(void);
void ui_DinoGamePage_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
