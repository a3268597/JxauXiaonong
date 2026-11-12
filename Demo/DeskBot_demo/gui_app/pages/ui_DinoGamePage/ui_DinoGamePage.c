#include "ui_DinoGamePage.h"
#include "../../images/ui_img_dino_sprites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/*
 * T-Rex (chrome://dino / chrome://network-error) 的 LVGL 还原实现。
 *
 * 数值全部取自 Chromium 源码 components/neterror/resources/dino_game/:
 *   offline.ts                      速度/加速/间距/日夜/计分系数/开场保护
 *   trex.ts                         重力/初速/跳跃上下限/身体/碰撞盒/动画帧率
 *   offline_sprite_definitions.ts   障碍表(尺寸/minGap/multipleSpeed/minSpeed/碰撞盒)
 *   obstacle.ts                     并排盒体修正/getGap()/speedOffset
 *   horizon.ts / horizon_line.ts    生成条件/地面线两种起伏
 *   distance_meter.ts               字形 10x13/DEST_WIDTH 11/里程碑闪烁
 *   game_over_panel.ts              GAME OVER 文字与 restart 8 帧动画
 *   night_mode.ts / cloud.ts        月亮相位/星星/云速度与间距
 *
 * 本设备 320x240, 原版画布 600x150。采用"方案A": 精灵 1:1 不缩放, 只把横向
 * 跑道压缩到 320 —— 横向位移统一乘 DINO_X_SCALE, 保证"障碍从右边缘走到恐龙
 * 身前"的反应时间与原版一致; 纵向物理保持原值(障碍高度/跳跃高度都是 1:1),
 * 计分系数反向除以 X_SCALE, 使加分节奏与原版相同。
 */

/* ---------- 世界与压缩 ---------- */
#define DINO_FPS                 60
#define DINO_MS_PER_FRAME        (1000.0 / DINO_FPS)
#define DINO_CANVAS_W            UI_SCREEN_WIDTH
#define DINO_X_SCALE             (320.0 / 600.0)
#define DINO_GROUND_Y            196     /* 原版地面线 y=140 (150 - bottomPad 10) */
#define DINO_TREX_X              50      /* trex.ts startXPos */
#define DINO_TREX_W              44
#define DINO_TREX_H              47
#define DINO_TREX_DUCK_W         59
#define DINO_TREX_GROUND_TOP     (DINO_GROUND_Y - DINO_TREX_H)

/* ---------- 物理 (offline.ts / trex.ts) ---------- */
#define DINO_SPEED_INIT          6.0
#define DINO_SPEED_MAX           13.0
#define DINO_ACCEL               0.001
#define DINO_GRAVITY             0.6
#define DINO_JUMP_V0             (-10.0)
#define DINO_MIN_JUMP_PX         30      /* groundYPos - minJumpHeight(30) */
#define DINO_MAX_JUMP_PX         63      /* groundYPos(93) - maxJumpHeight(30) */
#define DINO_DROP_VELOCITY       (-5.0)
#define DINO_SPEED_DROP_COEF     3.0
#define DINO_CLEAR_TIME_MS       3000.0f
#define DINO_GAMEOVER_CLEAR_MS   1200
#define DINO_INTRO_MS            1500
#define DINO_MAX_BLINK           3

/* ---------- 计分 (distance_meter.ts) ---------- */
#define DINO_DIST_COEFFICIENT    0.025
#define DINO_ACHIEVEMENT_DIST    100
#define DINO_FLASH_MS            250
#define DINO_FLASH_ITERATIONS    3
#define DINO_BASE_SCORE_UNITS    5
#define DINO_MAX_UNITS           6
#define DINO_GLYPH_W             10
#define DINO_GLYPH_PITCH         11
#define DINO_SCORE_Y             8
#define DINO_HI_SLOTS            8       /* "HI" + 空格 + 最多 6 位 */

/* ---------- 日夜与背景 ---------- */
#define DINO_INVERT_DISTANCE     700
#define DINO_INVERT_HOLD_MS      12000
#define DINO_NIGHT_FADE_MS       1500.0f
#define DINO_CLOUD_MAX           6
#define DINO_CLOUD_SPEED         0.2
#define DINO_CLOUD_W             46
#define DINO_CLOUD_H             14
#define DINO_MOON_SPEED          0.25
#define DINO_STAR_SPEED          0.3
#define DINO_STAR_MAX_Y          70
#define DINO_HORIZON_W           600
#define DINO_HORIZON_H           12

/* ---------- 障碍生成 ---------- */
#define DINO_GAP_COEFF           0.6
#define DINO_MAX_GAP_COEFF       1.5
#define DINO_MAX_OBST_LENGTH     3
#define DINO_MAX_OBST_DUP        2
#define DINO_OBST_POOL           6
#define DINO_OBST_TYPES          3

/* ---------- 配色: 日间为 sheet 原色, 夜间为 filter:invert(1) 的结果 ---------- */
#define DINO_BG_DAY              0xF7F7F7
#define DINO_BG_NIGHT            0x080808
#define DINO_MAIN_DAY            0x535353
#define DINO_MAIN_NIGHT          0xACACAC
#define DINO_LIGHT_DAY           0xDADADA
#define DINO_LIGHT_NIGHT         0x252525
#define DINO_STAR_NIGHT          0x454545

#define DINO_DUCK_ZONE_TOP       160     /* 屏幕下缘触控区 = 下蹲 */
#define DINO_HI_FILE             "dino_hi.conf"
#define DINO_TIMER_PERIOD_MS     16

typedef enum {
    DINO_ST_WAITING,
    DINO_ST_RUNNING,
    DINO_ST_CRASHED,
} dino_status_e;

typedef struct { int32_t x, y, w, h; } dino_box_t;

typedef struct {
    const lv_image_dsc_t *frames[3]; /* 仙人掌: 1/2/3 并排; 翼龙: 拍翅 2 帧 */
    int32_t unit_w, h;
    double  multiple_speed;
    double  min_speed;
    int32_t min_gap;
    int32_t num_frames;              /* >0 = 动画障碍 */
    uint32_t frame_rate_ms;
    double  speed_offset;
    const dino_box_t *boxes;
    int32_t box_count;
    int32_t y_up[3];                 /* 距地面高度, 0 = 贴地 */
    int32_t y_count;
} dino_obst_def_t;

typedef struct {
    lv_obj_t *img;
    const dino_obst_def_t *def;
    dino_box_t boxes[3];
    int32_t x, y, w, h;
    int32_t size;
    int32_t y_band;
    double  sub_pos;
    int32_t anim_frame;
    uint32_t frame_timer;
    int32_t gap;
    double  speed_offset;
    bool    used;
    bool    following_created;
} dino_obst_t;

typedef struct {
    lv_obj_t *img;
    int32_t x, y;
    bool used;
} dino_cloud_t;

typedef struct {
    lv_obj_t *img;
    int32_t x, y;
} dino_star_t;

///////////////////// VARIABLES ////////////////////

static lv_obj_t * dino_page;
static lv_obj_t * dino_trex_img;
static lv_obj_t * dino_horizon_img[2];
static lv_obj_t * dino_gameover_text;
static lv_obj_t * dino_restart_img;
static lv_obj_t * dino_moon_img;
static lv_obj_t * dino_score_img[DINO_MAX_UNITS];
static lv_obj_t * dino_hi_img[DINO_HI_SLOTS];
static lv_timer_t * dino_timer;

static dino_obst_t dino_obst[DINO_OBST_POOL];
static dino_cloud_t dino_cloud[DINO_CLOUD_MAX];
static dino_star_t dino_star[2];

static const lv_image_dsc_t * DINO_GLYPHS[12] = {
    &dino_sp_glyph_0, &dino_sp_glyph_1, &dino_sp_glyph_2, &dino_sp_glyph_3,
    &dino_sp_glyph_4, &dino_sp_glyph_5, &dino_sp_glyph_6, &dino_sp_glyph_7,
    &dino_sp_glyph_8, &dino_sp_glyph_9, &dino_sp_glyph_H, &dino_sp_glyph_I,
};
static const lv_image_dsc_t * DINO_MOON_PHASES[4] = {
    &dino_sp_moon_0, &dino_sp_moon_1, &dino_sp_moon_2, &dino_sp_moon_3,
};
static const lv_image_dsc_t * DINO_RESTART_FRAMES[8] = {
    &dino_sp_restart_0, &dino_sp_restart_1, &dino_sp_restart_2, &dino_sp_restart_3,
    &dino_sp_restart_4, &dino_sp_restart_5, &dino_sp_restart_6, &dino_sp_restart_7,
};

/* trex.ts collisionBoxes */
static const dino_box_t TREX_BOXES_RUN[] = {
    {22, 0, 17, 16}, {1, 18, 30, 9}, {10, 35, 14, 8},
    {1, 24, 29, 5}, {5, 30, 21, 4}, {9, 34, 15, 4},
};
static const dino_box_t TREX_BOXES_DUCK[] = { {1, 18, 55, 25} };

/* offline_sprite_definitions.ts obstacles */
static const dino_box_t BOXES_CACTUS_SMALL[] = {
    {0, 7, 5, 27}, {4, 0, 6, 34}, {10, 4, 7, 14},
};
static const dino_box_t BOXES_CACTUS_LARGE[] = {
    {0, 12, 7, 38}, {8, 0, 7, 49}, {13, 10, 10, 38},
};
static const dino_box_t BOXES_PTERO[] = {
    {15, 15, 16, 5}, {18, 21, 24, 6}, {2, 14, 4, 3}, {6, 10, 4, 7}, {10, 8, 6, 9},
};

static const dino_obst_def_t DINO_OBST_DEFS[DINO_OBST_TYPES] = {
    {   /* cactusSmall: 17x35, multipleSpeed 4, minGap 120, minSpeed 0 */
        {&dino_sp_cactus_small_1, &dino_sp_cactus_small_2, &dino_sp_cactus_small_3},
        17, 35, 4.0, 0.0, 120, 0, 0, 0.0, BOXES_CACTUS_SMALL, 3, {0}, 1,
    },
    {   /* cactusLarge: 25x50, multipleSpeed 7, minGap 120, minSpeed 0 */
        {&dino_sp_cactus_large_1, &dino_sp_cactus_large_2, &dino_sp_cactus_large_3},
        25, 50, 7.0, 0.0, 120, 0, 0, 0.0, BOXES_CACTUS_LARGE, 3, {0}, 1,
    },
    {   /* pterodactyl: 46x40, minSpeed 8.5, minGap 150, 2 帧 @1000/6, speedOffset .8
         * 原版桌面端高度带 yPos [100,75,50] -> 距地面 [0,25,50] */
        {&dino_sp_ptero_0, &dino_sp_ptero_1, NULL},
        46, 40, 999.0, 8.5, 150, 2, 1000 / 6, 0.8, BOXES_PTERO, 5, {0, 25, 50}, 3,
    },
};

static dino_status_e dino_status = DINO_ST_WAITING;
static double dino_speed;            /* 原版单位 px/frame */
static double dino_distance;         /* 累计像素 */
static double dino_vy;
static double dino_scroll_acc;
static int32_t dino_trex_y;
static int32_t dino_trex_x;
static int32_t dino_score;
static int32_t dino_hi_score;
static int32_t dino_score_units = DINO_BASE_SCORE_UNITS;
static int32_t dino_horizon_x[2];
static uint32_t dino_running_time;
static float dino_clear_time = DINO_CLEAR_TIME_MS;
static uint32_t dino_last_tick;
static uint32_t dino_crash_tick;
static uint32_t dino_intro_elapsed;
static uint32_t dino_anim_timer;
static uint32_t dino_blink_timer;
static uint32_t dino_flash_timer;
static uint32_t dino_night_timer;
static uint32_t dino_restart_timer;
static int32_t dino_anim_frame;
static int32_t dino_blink_count;
static int32_t dino_moon_x;
static int32_t dino_moon_phase;
static int32_t dino_flash_iterations;
static int32_t dino_obst_history[4];
static int32_t dino_history_len;
static float dino_night_t;
static bool dino_activated;
static bool dino_intro_playing;
static bool dino_jumping;
static bool dino_ducking;
static bool dino_speed_drop;
static bool dino_duck_held;
static bool dino_reached_min_jump;
static bool dino_night_active;
static bool dino_achievement;
static bool dino_rand_seeded;
static double dino_cloud_scroll;
static double dino_night_scroll;
static double dino_star_scroll[2];

///////////////////// HELPERS ////////////////////

static int32_t _dino_rand(int32_t min, int32_t max)
{
    if (max <= min) return min;
    return min + (int32_t)(rand() % (max - min + 1));
}

static int32_t _dino_clamp255(int32_t v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static lv_color_t _dino_blend(uint32_t day, uint32_t night, float t)
{
    /* 注意: 必须先转成有符号数再相减, 否则 uint32_t 下溢会得到天文数字 */
    int32_t dr = (int32_t)((day >> 16) & 0xFF), dg = (int32_t)((day >> 8) & 0xFF), db = (int32_t)(day & 0xFF);
    int32_t nr = (int32_t)((night >> 16) & 0xFF), ng = (int32_t)((night >> 8) & 0xFF), nb = (int32_t)(night & 0xFF);
    int32_t r = _dino_clamp255(dr + (int32_t)((float)(nr - dr) * t));
    int32_t g = _dino_clamp255(dg + (int32_t)((float)(ng - dg) * t));
    int32_t b = _dino_clamp255(db + (int32_t)((float)(nb - db) * t));
    return lv_color_hex((uint32_t)((r << 16) | (g << 8) | b));
}

static lv_obj_t * _dino_make_img(lv_obj_t * parent, const lv_image_dsc_t * src)
{
    lv_obj_t * img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return img;
}

static void _dino_show(lv_obj_t * obj, bool show)
{
    if (!obj) return;
    if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static bool _dino_box_overlap(const dino_box_t * a, const dino_box_t * b)
{
    return (a->x < b->x + b->w) && (a->x + a->w > b->x) &&
           (a->y < b->y + b->h) && (a->y + a->h > b->y);
}

/* 白天/夜间共用一套素材: 原版的 1px 白色光晕已在切图时转成透明,
 * 因此整体 recolor 即可同时得到日间 #535353 与夜间 #ACACAC + 黑色光晕。 */
static void _dino_apply_palette(void)
{
    float t = dino_night_t;
    lv_color_t main_c = _dino_blend(DINO_MAIN_DAY, DINO_MAIN_NIGHT, t);
    lv_color_t light_c = _dino_blend(DINO_LIGHT_DAY, DINO_LIGHT_NIGHT, t);
    lv_color_t star_c = _dino_blend(DINO_LIGHT_DAY, DINO_STAR_NIGHT, t);

    lv_obj_set_style_bg_color(dino_page, _dino_blend(DINO_BG_DAY, DINO_BG_NIGHT, t), 0);

    lv_obj_t * mains[] = {dino_trex_img, dino_horizon_img[0], dino_horizon_img[1],
                          dino_gameover_text, dino_restart_img};
    for (unsigned i = 0; i < sizeof(mains) / sizeof(mains[0]); i++) {
        if (!mains[i]) continue;
        lv_obj_set_style_image_recolor(mains[i], main_c, 0);
        lv_obj_set_style_image_recolor_opa(mains[i], 255, 0);
    }
    for (int i = 0; i < DINO_OBST_POOL; i++) {
        if (!dino_obst[i].img) continue;
        lv_obj_set_style_image_recolor(dino_obst[i].img, main_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_obst[i].img, 255, 0);
    }
    for (int i = 0; i < DINO_MAX_UNITS; i++) {
        if (!dino_score_img[i]) continue;
        lv_obj_set_style_image_recolor(dino_score_img[i], main_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_score_img[i], 255, 0);
    }
    for (int i = 0; i < DINO_HI_SLOTS; i++) {
        if (!dino_hi_img[i]) continue;
        lv_obj_set_style_image_recolor(dino_hi_img[i], main_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_hi_img[i], 255, 0);
        lv_obj_set_style_image_opa(dino_hi_img[i], 204, 0);   /* globalAlpha .8 */
    }
    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        if (!dino_cloud[i].img) continue;
        lv_obj_set_style_image_recolor(dino_cloud[i].img, light_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_cloud[i].img, 255, 0);
    }
    if (dino_moon_img) {
        lv_obj_set_style_image_recolor(dino_moon_img, light_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_moon_img, 255, 0);
        lv_obj_set_style_image_opa(dino_moon_img, (lv_opa_t)(t * 255), 0);
    }
    for (int i = 0; i < 2; i++) {
        if (!dino_star[i].img) continue;
        lv_obj_set_style_image_recolor(dino_star[i].img, star_c, 0);
        lv_obj_set_style_image_recolor_opa(dino_star[i].img, 255, 0);
        lv_obj_set_style_image_opa(dino_star[i].img, (lv_opa_t)(t * 255), 0);
    }
}

static void _dino_load_hi(void)
{
    FILE * f = fopen(DINO_HI_FILE, "r");
    dino_hi_score = 0;
    if (f) {
        if (fscanf(f, "%d", &dino_hi_score) != 1) dino_hi_score = 0;
        fclose(f);
    }
    if (dino_hi_score < 0) dino_hi_score = 0;
}

static void _dino_save_hi(void)
{
    FILE * f = fopen(DINO_HI_FILE, "w");
    if (!f) return;
    fprintf(f, "%d\n", (int)dino_hi_score);
    fclose(f);
}

///////////////////// 背景 ////////////////////

static void _dino_update_horizon(int32_t move)
{
    for (int i = 0; i < 2; i++) {
        dino_horizon_x[i] -= move;
        if (dino_horizon_x[i] <= -DINO_HORIZON_W) {
            dino_horizon_x[i] += DINO_HORIZON_W * 2;
            if (_dino_rand(0, 100) > 50) lv_image_set_src(dino_horizon_img[i], &dino_sp_horizon_1);
            else lv_image_set_src(dino_horizon_img[i], &dino_sp_horizon_0);
        }
        lv_obj_set_x(dino_horizon_img[i], dino_horizon_x[i]);
    }
}

static void _dino_update_clouds(double frames)
{
    int32_t rightmost = DINO_CANVAS_W + 400;
    int32_t cdx;

    /* cloud.ts: 云以 bgCloudSpeed(0.2) 倍地平线速度漂移, 亚像素需要累加 */
    dino_cloud_scroll += dino_speed * DINO_CLOUD_SPEED * frames * DINO_X_SCALE;
    cdx = (int32_t)dino_cloud_scroll;
    dino_cloud_scroll -= cdx;

    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        if (!dino_cloud[i].used) continue;
        dino_cloud[i].x -= cdx;
        if (dino_cloud[i].x + DINO_CLOUD_W < 0) {
            dino_cloud[i].used = false;
            _dino_show(dino_cloud[i].img, false);
            continue;
        }
        lv_obj_set_x(dino_cloud[i].img, dino_cloud[i].x);
        if (dino_cloud[i].x < rightmost) rightmost = dino_cloud[i].x;
    }
    /* cloud.ts: MIN_CLOUD_GAP 100 ~ MAX_CLOUD_GAP 400 */
    if (rightmost > DINO_CANVAS_W) {
        for (int i = 0; i < DINO_CLOUD_MAX; i++) {
            if (dino_cloud[i].used) continue;
            dino_cloud[i].used = true;
            dino_cloud[i].x = DINO_CANVAS_W + _dino_rand(0, 60);
            dino_cloud[i].y = DINO_GROUND_Y - _dino_rand(70, 110);
            lv_obj_set_pos(dino_cloud[i].img, dino_cloud[i].x, dino_cloud[i].y);
            _dino_show(dino_cloud[i].img, true);
            break;
        }
    }
}

static void _dino_update_night_bg(double frames)
{
    if (dino_night_t <= 0.0f && !dino_night_active) {
        _dino_show(dino_moon_img, false);
        for (int i = 0; i < 2; i++) _dino_show(dino_star[i].img, false);
        return;
    }
    _dino_show(dino_moon_img, true);
    /* night_mode.ts: 月亮 0.25 px/frame, 星星 0.3 px/frame (原版单位, 需横向压缩) */
    dino_night_scroll += DINO_MOON_SPEED * DINO_X_SCALE * frames;
    if (dino_night_scroll >= 1.0) {
        int32_t step = (int32_t)dino_night_scroll;
        dino_night_scroll -= step;
        dino_moon_x -= step;
    }
    if (dino_moon_x < -20) dino_moon_x = DINO_CANVAS_W + 20;
    lv_obj_set_x(dino_moon_img, dino_moon_x);

    for (int i = 0; i < 2; i++) {
        _dino_show(dino_star[i].img, true);
        dino_star_scroll[i] += DINO_STAR_SPEED * DINO_X_SCALE * frames;
        if (dino_star_scroll[i] >= 1.0) {
            int32_t step = (int32_t)dino_star_scroll[i];
            dino_star_scroll[i] -= step;
            dino_star[i].x -= step;
        }
        if (dino_star[i].x < -9) {
            dino_star[i].x = DINO_CANVAS_W + _dino_rand(0, 80);
            dino_star[i].y = DINO_GROUND_Y - DINO_HORIZON_H - _dino_rand(0, DINO_STAR_MAX_Y);
        }
        lv_obj_set_pos(dino_star[i].img, dino_star[i].x, dino_star[i].y);
    }
}

///////////////////// T-Rex ////////////////////

static void _dino_set_trex_frame(void)
{
    const lv_image_dsc_t * src;

    if (dino_status == DINO_ST_CRASHED) src = &dino_sp_dino_crash;
    else if (dino_ducking) src = dino_anim_frame ? &dino_sp_dino_duck2 : &dino_sp_dino_duck1;
    else if (dino_jumping) src = &dino_sp_dino_stand;
    else if (dino_status == DINO_ST_WAITING) {
        src = dino_anim_frame ? &dino_sp_dino_blink : &dino_sp_dino_stand;
    }
    else src = dino_anim_frame ? &dino_sp_dino_run2 : &dino_sp_dino_run1;

    if (lv_image_get_src(dino_trex_img) != (const void *)src) lv_image_set_src(dino_trex_img, src);
    lv_obj_set_pos(dino_trex_img, dino_trex_x, dino_trex_y);
}

static void _dino_start_jump(void)
{
    if (dino_jumping) return;
    dino_jumping = true;
    dino_ducking = false;
    dino_speed_drop = false;
    dino_reached_min_jump = false;
    dino_vy = DINO_JUMP_V0 - (dino_speed / 10.0);   /* trex.ts startJump */
}

static void _dino_update_jump(double frames)
{
    int32_t height;

    if (dino_speed_drop) dino_trex_y += (int32_t)(dino_vy * DINO_SPEED_DROP_COEF * frames);
    else dino_trex_y += (int32_t)(dino_vy * frames);
    dino_vy += DINO_GRAVITY * frames;

    height = DINO_TREX_GROUND_TOP - dino_trex_y;
    if (height > DINO_MIN_JUMP_PX) dino_reached_min_jump = true;
    if (height > DINO_MAX_JUMP_PX || dino_speed_drop) {
        if (dino_reached_min_jump && dino_vy < DINO_DROP_VELOCITY) dino_vy = DINO_DROP_VELOCITY;
    }
    if (dino_trex_y >= DINO_TREX_GROUND_TOP) {
        dino_trex_y = DINO_TREX_GROUND_TOP;
        dino_vy = 0;
        dino_jumping = false;
        if (dino_speed_drop) {
            dino_speed_drop = false;
            dino_ducking = dino_duck_held;   /* 原版: 速降落地后若仍按住则转为下蹲 */
        }
    }
}

///////////////////// 障碍 ////////////////////

static const lv_image_dsc_t * _dino_obst_src(const dino_obst_t * o)
{
    if (o->def->num_frames > 0) return o->def->frames[o->anim_frame];
    return o->def->frames[o->size - 1];
}

static void _dino_obst_place(dino_obst_t * o)
{
    const dino_obst_def_t * d = o->def;

    o->w = d->unit_w * o->size;
    o->h = d->h;
    o->y = DINO_GROUND_Y - o->h - d->y_up[o->y_band];
    for (int i = 0; i < d->box_count && i < 3; i++) o->boxes[i] = d->boxes[i];
    if (o->size > 1 && d->box_count >= 3) {
        o->boxes[1].w = o->w - o->boxes[0].w - o->boxes[2].w;
        o->boxes[2].x = o->w - o->boxes[2].w;
    }
    lv_image_set_src(o->img, _dino_obst_src(o));
    lv_obj_set_pos(o->img, o->x, o->y);
}

/*
 * 一组并排障碍能否被跳过。
 *
 * 起跳后高度 h(s) = v0*s - g/2*s^2 (s 为帧数), 要越过宽度 w、高度 h 的一排障碍,
 * 需要"身体高于 h 的那段时间里, 障碍相对恐龙水平推进的距离" >= w + 恐龙宽 + 余量。
 * 原版画布 600 宽、速度 6~13 px/frame 天然满足; 本设备横向压缩到 0.533 后,
 * 推进距离变小, 所以必须按实际物理反推允许的并排数, 否则会出现"物理上不可能
 * 跳过去"的组合(这正是三根大仙人掌无法逾越的原因)。
 */
static bool _dino_group_clearable(const dino_obst_def_t * d, int32_t size, double speed)
{
    double v0 = -(DINO_JUMP_V0) + speed / 10.0;      /* startJump 会按速度加力 */
    double disc = v0 * v0 - 2.0 * DINO_GRAVITY * d->h;
    double window_frames, travel;

    if (disc <= 0.0) return false;                   /* 连障碍高度都跳不到 */
    window_frames = 2.0 * sqrt(disc) / DINO_GRAVITY;
    travel = window_frames * speed * DINO_X_SCALE;
    return travel >= (double)(d->unit_w * size + DINO_TREX_W + 8);
}

static int32_t _dino_pick_obst_type(void)
{
    for (int tries = 0; tries < 12; tries++) {
        int32_t idx = _dino_rand(0, DINO_OBST_TYPES - 1);
        if (dino_speed < DINO_OBST_DEFS[idx].min_speed) continue;
        if (dino_history_len >= DINO_MAX_OBST_DUP) {
            bool dup = true;
            for (int i = 0; i < DINO_MAX_OBST_DUP; i++) {
                if (dino_obst_history[i] != idx) { dup = false; break; }
            }
            if (dup) continue;
        }
        return idx;
    }
    return -1;
}

static void _dino_spawn_obstacle(void)
{
    int32_t idx = _dino_pick_obst_type();
    dino_obst_t * o = NULL;

    if (idx < 0) return;
    for (int i = 0; i < DINO_OBST_POOL; i++) {
        if (!dino_obst[i].used) { o = &dino_obst[i]; break; }
    }
    if (!o) return;

    o->def = &DINO_OBST_DEFS[idx];
    o->size = _dino_rand(1, DINO_MAX_OBST_LENGTH);
    /* 先受原版 multipleSpeed 门槛约束, 再保证这一组在物理上真的跳得过去 */
    while (o->size > 1 && (o->def->multiple_speed > dino_speed ||
                           !_dino_group_clearable(o->def, o->size, dino_speed))) {
        o->size--;
    }
    o->y_band = (o->def->y_count > 1) ? _dino_rand(0, o->def->y_count - 1) : 0;
    o->anim_frame = 0;
    o->frame_timer = 0;
    o->sub_pos = 0;
    o->x = DINO_CANVAS_W;
    o->used = true;
    o->following_created = false;
    o->speed_offset = o->def->speed_offset
                          ? (((rand() & 1) ? 1 : -1) * o->def->speed_offset) : 0.0;
    _dino_obst_place(o);
    _dino_show(o->img, true);

    /* obstacle.ts getGap() */
    {
        int32_t min_gap = (int32_t)(o->w * dino_speed + o->def->min_gap * DINO_GAP_COEFF + 0.5);
        int32_t max_gap = (int32_t)(min_gap * DINO_MAX_GAP_COEFF + 0.5);
        o->gap = _dino_rand(min_gap, max_gap);
    }
    if (dino_history_len < (int32_t)(sizeof(dino_obst_history) / sizeof(dino_obst_history[0]))) {
        for (int i = dino_history_len; i > 0; i--) dino_obst_history[i] = dino_obst_history[i - 1];
        dino_history_len++;
    } else {
        for (int i = 3; i > 0; i--) dino_obst_history[i] = dino_obst_history[i - 1];
    }
    dino_obst_history[0] = idx;
}

static dino_obst_t * _dino_first_obstacle(void)
{
    dino_obst_t * first = NULL;

    for (int i = 0; i < DINO_OBST_POOL; i++) {
        if (!dino_obst[i].used) continue;
        if (!first || dino_obst[i].x < first->x) first = &dino_obst[i];
    }
    return first;
}

static dino_obst_t * _dino_last_obstacle(void)
{
    dino_obst_t * last = NULL;

    for (int i = 0; i < DINO_OBST_POOL; i++) {
        if (!dino_obst[i].used) continue;
        if (!last || dino_obst[i].x > last->x) last = &dino_obst[i];
    }
    return last;
}

static void _dino_update_obstacles(uint32_t dt_ms, double frames, int32_t move)
{
    for (int i = 0; i < DINO_OBST_POOL; i++) {
        dino_obst_t * o = &dino_obst[i];
        if (!o->used) continue;

        o->sub_pos += o->speed_offset * frames * DINO_X_SCALE;
        if (o->sub_pos >= 1.0 || o->sub_pos <= -1.0) {
            int32_t extra = (int32_t)o->sub_pos;
            o->sub_pos -= extra;
            o->x -= extra;
        }
        o->x -= move;
        if (o->def->num_frames) {
            o->frame_timer += dt_ms;
            if (o->frame_timer >= o->def->frame_rate_ms) {
                o->frame_timer = 0;
                o->anim_frame = (o->anim_frame + 1) % o->def->num_frames;
                lv_image_set_src(o->img, _dino_obst_src(o));
            }
        }
        if (o->x + o->w < 0) {
            o->used = false;
            _dino_show(o->img, false);
            continue;
        }
        lv_obj_set_x(o->img, o->x);
    }

    if ((float)dino_running_time < dino_clear_time) return;   /* 开局保护 */

    {
        dino_obst_t * last = _dino_last_obstacle();
        if (last) {
            /* horizon.ts: 最后一根走完自己的 gap 才补下一根 */
            if (!last->following_created && last->x + last->w > 0 &&
                (last->x + last->w + last->gap) < DINO_CANVAS_W) {
                _dino_spawn_obstacle();
                last->following_created = true;
            }
        } else {
            _dino_spawn_obstacle();
        }
    }
}

static bool _dino_check_collision(const dino_obst_t * o)
{
    const dino_box_t * tboxes;
    int tcount, bcount;
    dino_box_t trex_box = {dino_trex_x + 1, dino_trex_y + 1,
                           (dino_ducking ? DINO_TREX_DUCK_W : DINO_TREX_W) - 2,
                           DINO_TREX_H - 2};
    dino_box_t obst_box = {o->x + 1, o->y + 1, o->w - 2, o->h - 2};

    if (!_dino_box_overlap(&trex_box, &obst_box)) return false;

    if (dino_ducking) { tboxes = TREX_BOXES_DUCK; tcount = 1; }
    else { tboxes = TREX_BOXES_RUN; tcount = (int)(sizeof(TREX_BOXES_RUN) / sizeof(TREX_BOXES_RUN[0])); }
    bcount = o->def->box_count;

    for (int i = 0; i < tcount; i++) {
        dino_box_t a = {dino_trex_x + tboxes[i].x, dino_trex_y + tboxes[i].y,
                        tboxes[i].w, tboxes[i].h};
        for (int j = 0; j < bcount; j++) {
            dino_box_t b = {o->x + o->boxes[j].x, o->y + o->boxes[j].y,
                            o->boxes[j].w, o->boxes[j].h};
            if (_dino_box_overlap(&a, &b)) return true;
        }
    }
    return false;
}

///////////////////// 计分 ////////////////////

static void _dino_set_glyph(lv_obj_t * obj, int32_t ch)
{
    if (!obj) return;
    if (ch < 0) { _dino_show(obj, false); return; }
    lv_image_set_src(obj, DINO_GLYPHS[ch]);
    _dino_show(obj, true);
}

static void _dino_layout_score(void)
{
    int32_t x = DINO_CANVAS_W - DINO_GLYPH_PITCH * (dino_score_units + 1);
    int32_t hx = x - dino_score_units * 2 * DINO_GLYPH_W;

    for (int i = 0; i < DINO_MAX_UNITS; i++) lv_obj_set_pos(dino_score_img[i], x + i * DINO_GLYPH_PITCH, DINO_SCORE_Y);
    for (int i = 0; i < DINO_HI_SLOTS; i++) lv_obj_set_pos(dino_hi_img[i], hx + i * DINO_GLYPH_PITCH, DINO_SCORE_Y);
}

static void _dino_update_score_display(void)
{
    static const int32_t POW10[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    bool paint = !(dino_achievement && dino_flash_timer < DINO_FLASH_MS);
    int32_t div;

    if (dino_score > 99999 && dino_score_units == DINO_BASE_SCORE_UNITS) {
        dino_score_units++;
        _dino_layout_score();
    }
    div = POW10[dino_score_units - 1];
    for (int i = 0; i < DINO_MAX_UNITS; i++) {
        if (i >= dino_score_units) { _dino_set_glyph(dino_score_img[i], -1); continue; }
        _dino_set_glyph(dino_score_img[i], paint ? (dino_score / div) % 10 : -1);
        div /= 10;
    }
    /* HI 常驻: "HI" + 补零的高分, 空格位不绘制 */
    {
        int32_t slot = 0;
        _dino_set_glyph(dino_hi_img[slot++], 10);            /* H */
        _dino_set_glyph(dino_hi_img[slot++], 11);            /* I */
        _dino_set_glyph(dino_hi_img[slot++], -1);            /* 空格 */
        div = POW10[dino_score_units - 1];
        for (int i = 0; i < dino_score_units && slot < DINO_HI_SLOTS; i++) {
            _dino_set_glyph(dino_hi_img[slot++], (dino_hi_score / div) % 10);
            div /= 10;
        }
        for (; slot < DINO_HI_SLOTS; slot++) _dino_set_glyph(dino_hi_img[slot], -1);
    }
}

///////////////////// 状态 ////////////////////

static void _dino_clear_entities(void)
{
    for (int i = 0; i < DINO_OBST_POOL; i++) {
        dino_obst[i].used = false;
        _dino_show(dino_obst[i].img, false);
    }
    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        dino_cloud[i].used = false;
        _dino_show(dino_cloud[i].img, false);
    }
    dino_history_len = 0;
    dino_scroll_acc = 0;
    dino_cloud_scroll = 0;
    dino_night_scroll = 0;
    dino_star_scroll[0] = dino_star_scroll[1] = 0;
}

static void _dino_reset(void)
{
    dino_speed = DINO_SPEED_INIT;
    dino_distance = 0;
    dino_score = 0;
    dino_score_units = DINO_BASE_SCORE_UNITS;
    dino_vy = 0;
    dino_trex_y = DINO_TREX_GROUND_TOP;
    dino_trex_x = DINO_TREX_X;
    dino_running_time = 0;
    dino_status = DINO_ST_RUNNING;
    dino_jumping = false;
    dino_ducking = false;
    dino_speed_drop = false;
    dino_duck_held = false;
    dino_achievement = false;
    dino_flash_timer = 0;
    dino_flash_iterations = 0;
    dino_intro_playing = false;
    dino_intro_elapsed = 0;
    dino_night_active = false;
    dino_night_t = 0;
    dino_night_timer = 0;
    _dino_clear_entities();
    for (int i = 0; i < 2; i++) {
        dino_horizon_x[i] = i * DINO_HORIZON_W;
        lv_image_set_src(dino_horizon_img[i], i ? &dino_sp_horizon_1 : &dino_sp_horizon_0);
        lv_obj_set_pos(dino_horizon_img[i], dino_horizon_x[i], DINO_GROUND_Y - DINO_HORIZON_H);
        _dino_show(dino_horizon_img[i], true);
    }
    _dino_show(dino_gameover_text, false);
    _dino_show(dino_restart_img, false);
    _dino_show(dino_moon_img, false);
    for (int i = 0; i < 2; i++) _dino_show(dino_star[i].img, false);
    _dino_apply_palette();
    _dino_layout_score();
    _dino_update_score_display();
    _dino_set_trex_frame();
}

static void _dino_game_over(void)
{
    dino_status = DINO_ST_CRASHED;
    dino_jumping = false;
    dino_ducking = false;
    dino_speed_drop = false;
    dino_crash_tick = lv_tick_get();
    dino_restart_timer = 0;
    if (dino_score > dino_hi_score) {
        dino_hi_score = dino_score;
        _dino_save_hi();
    }
    dino_clear_time *= 1.2f;   /* offline.ts: 下一次开局保护 ×1.2 */
    _dino_show(dino_gameover_text, true);
    _dino_show(dino_restart_img, true);
    lv_image_set_src(dino_restart_img, DINO_RESTART_FRAMES[0]);
    _dino_set_trex_frame();
}

static void _dino_activate(void)
{
    dino_activated = true;
    dino_intro_playing = true;
    dino_intro_elapsed = 0;
    dino_trex_x = 0;
    dino_status = DINO_ST_RUNNING;
}

///////////////////// 主循环 ////////////////////

static void _dino_game_timer_cb(lv_timer_t * timer)
{
    uint32_t now = lv_tick_get();
    uint32_t dt_ms = dino_last_tick ? (now - dino_last_tick) : (uint32_t)DINO_MS_PER_FRAME;
    double frames;
    int32_t move;

    LV_UNUSED(timer);
    if (dt_ms > 50) dt_ms = 50;
    dino_last_tick = now;
    frames = (double)dt_ms / DINO_MS_PER_FRAME;

    /* 未激活: 原版会眨眼 maxBlinkCount 次 */
    if (!dino_activated) {
        dino_blink_timer += dt_ms;
        if (dino_blink_timer >= 333 && dino_blink_count < DINO_MAX_BLINK * 2) {
            dino_blink_timer = 0;
            dino_blink_count++;
            dino_anim_frame = !dino_anim_frame;
            _dino_set_trex_frame();
        }
        return;
    }

    if (dino_status == DINO_ST_CRASHED) {
        dino_restart_timer += dt_ms;
        if (dino_restart_timer < 875) {
            int32_t f = (int32_t)(dino_restart_timer / (875 / 8));
            if (f > 7) f = 7;
            lv_image_set_src(dino_restart_img, DINO_RESTART_FRAMES[f]);
        }
        return;
    }

    dino_running_time += dt_ms;

    if (dino_intro_playing) {
        dino_intro_elapsed += dt_ms;
        dino_trex_x = (int32_t)((float)dino_intro_elapsed / DINO_INTRO_MS * DINO_TREX_X);
        if (dino_intro_elapsed >= DINO_INTRO_MS) {
            dino_intro_playing = false;
            dino_trex_x = DINO_TREX_X;
        }
        _dino_set_trex_frame();
        return;                        /* 原版: 入场动画期间地平线不动 */
    }

    /* 跑动 2 帧 @1000/12, 下蹲 2 帧 @1000/8 */
    dino_anim_timer += dt_ms;
    if (dino_anim_timer >= (dino_ducking ? 1000 / 8 : 1000 / 12)) {
        dino_anim_timer = 0;
        dino_anim_frame = !dino_anim_frame;
    }
    if (dino_jumping) _dino_update_jump(frames);

    dino_scroll_acc += dino_speed * frames * DINO_X_SCALE;
    move = (int32_t)dino_scroll_acc;
    dino_scroll_acc -= move;
    if (move <= 0) {
        _dino_set_trex_frame();
        return;
    }

    dino_distance += move / DINO_X_SCALE;
    if (dino_speed < DINO_SPEED_MAX) dino_speed += DINO_ACCEL * frames;

    _dino_update_horizon(move);
    _dino_update_clouds(frames);
    _dino_update_night_bg(frames);
    _dino_update_obstacles(dt_ms, frames, move);

    /* 分数 = 像素 * 0.025 / 横向压缩, 与原版加分节奏一致 */
    {
        int32_t actual = (int32_t)(dino_distance * DINO_DIST_COEFFICIENT / DINO_X_SCALE + 0.5);
        if (actual != dino_score) {
            int32_t prev = dino_score;
            dino_score = actual;
            /* 用"是否跨过阈值"判断, 一帧跳多格也不会漏 */
            if (!dino_achievement && dino_score / DINO_ACHIEVEMENT_DIST > prev / DINO_ACHIEVEMENT_DIST) {
                dino_achievement = true;
                dino_flash_timer = 0;
                dino_flash_iterations = 0;
            }
            if (!dino_night_active &&
                dino_score / DINO_INVERT_DISTANCE > prev / DINO_INVERT_DISTANCE) {
                dino_night_active = true;
                dino_night_timer = 0;
                dino_moon_phase = (dino_moon_phase + 1) % 4;
                lv_image_set_src(dino_moon_img, DINO_MOON_PHASES[dino_moon_phase]);
                dino_moon_x = DINO_CANVAS_W - 30;
            }
            _dino_update_score_display();
        }
    }

    if (dino_achievement) {
        dino_flash_timer += dt_ms;
        if (dino_flash_timer >= DINO_FLASH_MS * 2) {
            dino_flash_timer = 0;
            if (++dino_flash_iterations >= DINO_FLASH_ITERATIONS) {
                dino_achievement = false;
                dino_flash_iterations = 0;
                _dino_update_score_display();
            }
        }
        if (dino_flash_timer == 0) _dino_update_score_display();
    }
    if (dino_night_active) {
        dino_night_timer += dt_ms;
        if (dino_night_timer >= DINO_INVERT_HOLD_MS) {
            dino_night_active = false;
            dino_night_timer = 0;
        }
    }
    {
        float target = dino_night_active ? 1.0f : 0.0f;
        float step = (float)dt_ms / DINO_NIGHT_FADE_MS;
        if (dino_night_t < target) dino_night_t = (target - dino_night_t < step) ? target : dino_night_t + step;
        else if (dino_night_t > target) dino_night_t = (dino_night_t - target < step) ? target : dino_night_t - step;
        _dino_apply_palette();
    }

    {
        dino_obst_t * first = _dino_first_obstacle();
        if (first && _dino_check_collision(first)) {   /* 原版只检测第一个障碍 */
            _dino_game_over();
            return;
        }
    }

    _dino_set_trex_frame();
}

///////////////////// 输入 ////////////////////

static void _dino_press_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_indev_get_act();
    lv_point_t p = {0, 0};
    bool want_duck;

    if (indev) lv_indev_get_point(indev, &p);
    want_duck = (p.y >= DINO_DUCK_ZONE_TOP);

    if (code == LV_EVENT_PRESSED) {
        if (dino_status == DINO_ST_CRASHED) {
            if (lv_tick_elaps(dino_crash_tick) < DINO_GAMEOVER_CLEAR_MS) return;
            _dino_reset();
            _dino_activate();
            return;
        }
        if (!dino_activated) {
            dino_duck_held = want_duck;
            _dino_activate();
            if (!want_duck) _dino_start_jump();
            return;
        }
        dino_duck_held = want_duck;
        if (want_duck) {
            if (dino_jumping) dino_speed_drop = true;
            else dino_ducking = true;
        } else {
            dino_ducking = false;
            _dino_start_jump();
        }
        _dino_set_trex_frame();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        dino_duck_held = false;
        if (dino_ducking) {
            dino_ducking = false;
            _dino_set_trex_frame();
        }
    }
}

static void _gesture_cb(lv_event_t * e)
{
    lv_dir_t dir;

    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        lv_lib_pm_OpenPrePage(&page_manager);
    }
}

///////////////////// SCREEN init ////////////////////

void ui_DinoGamePage_init(void)
{
    if (!dino_rand_seeded) {
        srand((unsigned int)time(NULL));
        dino_rand_seeded = 1;
    }
    _dino_load_hi();

    dino_page = lv_obj_create(NULL);
    lv_obj_remove_flag(dino_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dino_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(dino_page, lv_color_hex(DINO_BG_DAY), 0);
    lv_obj_set_style_bg_opa(dino_page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(dino_page, 0, 0);
    lv_obj_set_style_border_width(dino_page, 0, 0);

    /* 夜空元素 */
    dino_moon_img = _dino_make_img(dino_page, DINO_MOON_PHASES[0]);
    lv_obj_set_y(dino_moon_img, DINO_GROUND_Y - 110);
    for (int i = 0; i < 2; i++) {
        dino_star[i].img = _dino_make_img(dino_page, i ? &dino_sp_star_1 : &dino_sp_star_0);
        dino_star[i].x = DINO_CANVAS_W + i * 90;
        dino_star[i].y = DINO_GROUND_Y - 40 - i * 30;
        lv_obj_set_pos(dino_star[i].img, dino_star[i].x, dino_star[i].y);
    }
    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        dino_cloud[i].img = _dino_make_img(dino_page, &dino_sp_cloud);
        dino_cloud[i].used = false;
    }

    /* 地面线 */
    for (int i = 0; i < 2; i++) {
        dino_horizon_img[i] = _dino_make_img(dino_page, i ? &dino_sp_horizon_1 : &dino_sp_horizon_0);
        dino_horizon_x[i] = i * DINO_HORIZON_W;
        lv_obj_set_pos(dino_horizon_img[i], dino_horizon_x[i], DINO_GROUND_Y - DINO_HORIZON_H);
    }

    /* 障碍池 */
    for (int i = 0; i < DINO_OBST_POOL; i++) {
        dino_obst[i].img = _dino_make_img(dino_page, &dino_sp_cactus_small_1);
        dino_obst[i].used = false;
    }

    /* T-Rex */
    dino_trex_img = _dino_make_img(dino_page, &dino_sp_dino_stand);
    dino_trex_y = DINO_TREX_GROUND_TOP;
    dino_trex_x = DINO_TREX_X;
    lv_obj_set_pos(dino_trex_img, dino_trex_x, dino_trex_y);
    lv_obj_clear_flag(dino_trex_img, LV_OBJ_FLAG_HIDDEN);

    /* 分数与 HI */
    for (int i = 0; i < DINO_MAX_UNITS; i++) {
        dino_score_img[i] = _dino_make_img(dino_page, DINO_GLYPHS[0]);
    }
    for (int i = 0; i < DINO_HI_SLOTS; i++) {
        dino_hi_img[i] = _dino_make_img(dino_page, DINO_GLYPHS[0]);
    }

    /* 结束面板 */
    dino_gameover_text = _dino_make_img(dino_page, &dino_sp_text_gameover);
    lv_obj_set_pos(dino_gameover_text, (DINO_CANVAS_W - 191) / 2, DINO_GROUND_Y - 60);
    dino_restart_img = _dino_make_img(dino_page, DINO_RESTART_FRAMES[0]);
    lv_obj_set_pos(dino_restart_img, (DINO_CANVAS_W - 36) / 2, DINO_GROUND_Y - 44);

    lv_obj_add_event_cb(dino_page, _dino_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(dino_page, _dino_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(dino_page, _dino_press_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(dino_page, _gesture_cb, LV_EVENT_GESTURE, NULL);

    dino_status = DINO_ST_WAITING;
    dino_activated = false;
    dino_last_tick = 0;
    dino_clear_time = DINO_CLEAR_TIME_MS;
    _dino_reset();
    dino_activated = false;
    dino_status = DINO_ST_WAITING;
    dino_blink_count = 0;
    dino_blink_timer = 0;
    _dino_set_trex_frame();

    dino_timer = lv_timer_create(_dino_game_timer_cb, DINO_TIMER_PERIOD_MS, NULL);

    lv_scr_load_anim(dino_page, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);
}

void ui_DinoGamePage_deinit(void)
{
    if (dino_timer) {
        lv_timer_del(dino_timer);
        dino_timer = NULL;
    }
}
