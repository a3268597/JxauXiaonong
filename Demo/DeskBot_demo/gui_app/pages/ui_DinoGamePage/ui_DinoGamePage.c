#include "ui_DinoGamePage.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
// #region debug-point A:diagnostic-dependencies
#include <curl/curl.h>
#include "../../../lvgl/src/draw/lv_image_decoder_private.h"
// #endregion

///////////////////// VARIABLES ////////////////////

#define DINO_TOPBAR_H 30
#define DINO_PLAY_TOP 38
#define DINO_GROUND_Y 196
#define DINO_PLAYER_X 34
#define DINO_PLAYER_W 34
#define DINO_PLAYER_H 37
#define DINO_CACTUS_MAX 4
#define DINO_GROUND_SEG_MAX 8
#define DINO_CLOUD_MAX 2
#define DINO_SPEED_INIT 95
#define DINO_SPEED_MAX 235
#define DINO_SPEED_STEP_SCORE 120
#define DINO_SPEED_STEP_VALUE 10
#define DINO_GRAVITY 1500
#define DINO_JUMP_VELOCITY 575
#define DINO_HINT_BLINK_MS 420

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *arm_top;
    lv_obj_t *arm_mid;
    lv_obj_t *extra_obj;
    lv_obj_t *extra_arm_top;
    lv_obj_t *extra_arm_mid;
    int32_t x;
    int32_t w;
    int32_t h;
    int32_t extra_x;
    int32_t extra_w;
    int32_t extra_h;
    uint8_t type;
    bool active;
} dino_cactus_t;

typedef struct {
    lv_obj_t *obj;
    int32_t x;
    int32_t y;
    int32_t w;
} dino_ground_seg_t;

typedef struct {
    lv_obj_t *obj;
    int32_t x;
    int32_t y;
    int32_t speed_divider;
} dino_cloud_t;

static lv_obj_t * dino_page;
static lv_obj_t * dino_hud_panel;
static lv_obj_t * dino_score_label;
static lv_obj_t * dino_speed_label;
static lv_obj_t * dino_player_obj;
static lv_obj_t * dino_player_shadow;
static lv_obj_t * dino_start_card;
static lv_obj_t * dino_start_hint;
static lv_obj_t * dino_gameover_card;
static lv_obj_t * dino_gameover_score;
static lv_obj_t * dino_gameover_hint;
static lv_timer_t * dino_timer;
static lv_obj_t * dino_sun_obj;

static dino_cactus_t dino_cactus[DINO_CACTUS_MAX];
static dino_ground_seg_t dino_ground_seg[DINO_GROUND_SEG_MAX];
static dino_cloud_t dino_cloud[DINO_CLOUD_MAX];

static int32_t dino_player_y;
static int32_t dino_player_vy;
static int32_t dino_score;
static int32_t dino_best_score;
static int32_t dino_speed;
static int32_t dino_spawn_gap;
static bool dino_in_air;
static bool dino_game_over;
static bool dino_started;
static bool dino_leg_toggle;
static bool dino_hint_toggle;
static uint32_t dino_last_tick;
static uint32_t dino_rand_seeded;
static uint32_t dino_last_hint_tick;
static uint32_t dino_page_open_tick;

///////////////////// FUNCTIONS ////////////////////

static int32_t _dino_ground_top(void)
{
    return DINO_GROUND_Y - DINO_PLAYER_H;
}

static void _dino_update_hud(void)
{
    char buf[32];

    sprintf(buf, "S %04d  B %04d", (int)(dino_score / 10), (int)(dino_best_score / 10));
    lv_label_set_text(dino_score_label, buf);

    sprintf(buf, "SPD %03d", (int)dino_speed);
    lv_label_set_text(dino_speed_label, buf);
}

static bool _dino_hit(int32_t ax, int32_t ay, int32_t aw, int32_t ah,
                      int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
    return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

static void _dino_update_player_pose(void)
{
    int32_t px = DINO_PLAYER_X;
    int32_t py = DINO_PLAY_TOP + dino_player_y;
    int32_t shadow_y = DINO_PLAY_TOP + DINO_GROUND_Y + 1;

    if (!dino_in_air && dino_started && !dino_game_over) {
        px += dino_leg_toggle ? 0 : 1;
    }

    lv_obj_set_pos(dino_player_obj, px, py);
    lv_obj_set_pos(dino_player_shadow, DINO_PLAYER_X + 4, shadow_y);
    lv_obj_set_size(dino_player_shadow, dino_in_air ? 16 : 24, 3);
}

static void _dino_set_card_hidden(lv_obj_t * card, bool hidden)
{
    if (!card) return;
    if (hidden) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void _dino_reset_world(void)
{
    dino_player_y = _dino_ground_top();
    dino_player_vy = 0;
    dino_score = 0;
    dino_speed = DINO_SPEED_INIT;
    dino_spawn_gap = 150;
    dino_in_air = false;
    dino_game_over = false;
    dino_started = false;
    dino_leg_toggle = false;
    dino_hint_toggle = false;
    dino_last_tick = 0;
    dino_last_hint_tick = 0;
    dino_page_open_tick = lv_tick_get();

    for (int i = 0; i < DINO_CACTUS_MAX; i++) {
        dino_cactus[i].active = false;
        dino_cactus[i].x = 0;
        dino_cactus[i].w = 0;
        dino_cactus[i].h = 0;
        dino_cactus[i].extra_x = 0;
        dino_cactus[i].extra_w = 0;
        dino_cactus[i].extra_h = 0;
        if (dino_cactus[i].obj) lv_obj_add_flag(dino_cactus[i].obj, LV_OBJ_FLAG_HIDDEN);
        if (dino_cactus[i].extra_obj) lv_obj_add_flag(dino_cactus[i].extra_obj, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < DINO_GROUND_SEG_MAX; i++) {
        dino_ground_seg[i].x = i * 46;
        dino_ground_seg[i].y = DINO_PLAY_TOP + DINO_GROUND_Y + ((i % 3 == 0) ? 1 : 0);
        if (dino_ground_seg[i].obj) lv_obj_set_pos(dino_ground_seg[i].obj, dino_ground_seg[i].x, dino_ground_seg[i].y);
    }

    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        dino_cloud[i].x = 40 + i * 130;
        dino_cloud[i].y = DINO_PLAY_TOP + 8 + i * 18;
        if (dino_cloud[i].obj) lv_obj_set_pos(dino_cloud[i].obj, dino_cloud[i].x, dino_cloud[i].y);
    }

    if (dino_start_hint) lv_obj_set_style_text_opa(dino_start_hint, 255, 0);
    if (dino_gameover_hint) lv_obj_set_style_text_opa(dino_gameover_hint, 255, 0);

    _dino_set_card_hidden(dino_gameover_card, true);
    _dino_set_card_hidden(dino_start_card, false);
    _dino_update_player_pose();
    _dino_update_hud();
}

static void _dino_spawn_cactus(void)
{
    for (int i = 0; i < DINO_CACTUS_MAX; i++) {
        if (dino_cactus[i].active) continue;

        dino_cactus[i].type = rand() % 3;
        dino_cactus[i].x = UI_SCREEN_WIDTH + 6;
        dino_cactus[i].active = true;
        dino_cactus[i].extra_w = 0;
        dino_cactus[i].extra_h = 0;

        if (!dino_cactus[i].obj) {
            dino_cactus[i].obj = lv_obj_create(dino_page);
            lv_obj_remove_flag(dino_cactus[i].obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].obj, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].obj, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].obj, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].obj, 255, 0);

            dino_cactus[i].arm_top = lv_obj_create(dino_cactus[i].obj);
            lv_obj_remove_flag(dino_cactus[i].arm_top, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].arm_top, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].arm_top, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].arm_top, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].arm_top, 255, 0);

            dino_cactus[i].arm_mid = lv_obj_create(dino_cactus[i].obj);
            lv_obj_remove_flag(dino_cactus[i].arm_mid, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].arm_mid, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].arm_mid, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].arm_mid, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].arm_mid, 255, 0);

            dino_cactus[i].extra_obj = lv_obj_create(dino_page);
            lv_obj_remove_flag(dino_cactus[i].extra_obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].extra_obj, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].extra_obj, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].extra_obj, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].extra_obj, 255, 0);

            dino_cactus[i].extra_arm_top = lv_obj_create(dino_cactus[i].extra_obj);
            lv_obj_remove_flag(dino_cactus[i].extra_arm_top, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].extra_arm_top, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].extra_arm_top, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].extra_arm_top, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].extra_arm_top, 255, 0);

            dino_cactus[i].extra_arm_mid = lv_obj_create(dino_cactus[i].extra_obj);
            lv_obj_remove_flag(dino_cactus[i].extra_arm_mid, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(dino_cactus[i].extra_arm_mid, 0, 0);
            lv_obj_set_style_radius(dino_cactus[i].extra_arm_mid, 0, 0);
            lv_obj_set_style_bg_color(dino_cactus[i].extra_arm_mid, lv_color_hex(0x4E9158), 0);
            lv_obj_set_style_bg_opa(dino_cactus[i].extra_arm_mid, 255, 0);
        }

        if (dino_cactus[i].type == 0) {
            dino_cactus[i].w = 12 + rand() % 8;
            dino_cactus[i].h = 24 + rand() % 10;
        } else if (dino_cactus[i].type == 1) {
            dino_cactus[i].w = 14 + rand() % 6;
            dino_cactus[i].h = 36 + rand() % 12;
        } else {
            dino_cactus[i].w = 12 + rand() % 6;
            dino_cactus[i].h = 22 + rand() % 8;
            dino_cactus[i].extra_w = 10 + rand() % 6;
            dino_cactus[i].extra_h = 18 + rand() % 8;
            dino_cactus[i].extra_x = dino_cactus[i].x + dino_cactus[i].w + 6;
        }

        lv_obj_set_size(dino_cactus[i].obj, dino_cactus[i].w, dino_cactus[i].h);
        lv_obj_set_pos(dino_cactus[i].obj,
                       dino_cactus[i].x,
                       DINO_PLAY_TOP + DINO_GROUND_Y - dino_cactus[i].h);

        lv_obj_set_size(dino_cactus[i].arm_top, 8, 4);
        lv_obj_set_pos(dino_cactus[i].arm_top, -4, 6);
        lv_obj_set_size(dino_cactus[i].arm_mid, 4, 10);
        lv_obj_set_pos(dino_cactus[i].arm_mid, 0, 6);
        lv_obj_clear_flag(dino_cactus[i].obj, LV_OBJ_FLAG_HIDDEN);

        if (dino_cactus[i].extra_w > 0) {
            lv_obj_set_size(dino_cactus[i].extra_obj, dino_cactus[i].extra_w, dino_cactus[i].extra_h);
            lv_obj_set_pos(dino_cactus[i].extra_obj,
                           dino_cactus[i].extra_x,
                           DINO_PLAY_TOP + DINO_GROUND_Y - dino_cactus[i].extra_h);
            lv_obj_set_size(dino_cactus[i].extra_arm_top, 6, 3);
            lv_obj_set_pos(dino_cactus[i].extra_arm_top, -3, 5);
            lv_obj_set_size(dino_cactus[i].extra_arm_mid, 3, 8);
            lv_obj_set_pos(dino_cactus[i].extra_arm_mid, 0, 5);
            lv_obj_clear_flag(dino_cactus[i].extra_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(dino_cactus[i].extra_obj, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
}

static void _dino_start_game(void)
{
    dino_started = true;
    _dino_set_card_hidden(dino_start_card, true);
}

static void _dino_jump(void)
{
    if (dino_game_over || dino_in_air) return;
    if (!dino_started) _dino_start_game();
    dino_player_vy = DINO_JUMP_VELOCITY;
    dino_in_air = true;
}

static void _dino_move_ground(int32_t move_x)
{
    for (int i = 0; i < DINO_GROUND_SEG_MAX; i++) {
        dino_ground_seg[i].x -= move_x;
        if (dino_ground_seg[i].x + dino_ground_seg[i].w < 0) {
            int32_t max_x = 0;
            for (int j = 0; j < DINO_GROUND_SEG_MAX; j++) {
                if (dino_ground_seg[j].x > max_x) max_x = dino_ground_seg[j].x;
            }
            dino_ground_seg[i].x = max_x + 22 + rand() % 28;
        }
        lv_obj_set_x(dino_ground_seg[i].obj, dino_ground_seg[i].x);
    }
}

static void _dino_move_clouds(int32_t move_x)
{
    for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        int32_t cloud_move = move_x / dino_cloud[i].speed_divider;
        if (cloud_move < 1) cloud_move = 1;
        dino_cloud[i].x -= cloud_move;
        if (dino_cloud[i].x + 48 < 0) {
            dino_cloud[i].x = UI_SCREEN_WIDTH + 20 + rand() % 50;
            dino_cloud[i].y = DINO_PLAY_TOP + 6 + rand() % 34;
        }
        lv_obj_set_pos(dino_cloud[i].obj, dino_cloud[i].x, dino_cloud[i].y);
    }
}

static void _dino_finish_game(void)
{
    char buf[48];

    dino_game_over = true;
    dino_started = false;
    if (dino_score > dino_best_score) dino_best_score = dino_score;

    sprintf(buf, "RUN %04d  BEST %04d", (int)(dino_score / 10), (int)(dino_best_score / 10));
    lv_label_set_text(dino_gameover_score, buf);
    _dino_set_card_hidden(dino_gameover_card, false);
    _dino_update_hud();

    if (dino_started == false && dino_gameover_hint) lv_obj_set_style_text_opa(dino_gameover_hint, 255, 0);
}

static void _dino_update_hint_blink(uint32_t now)
{
    lv_obj_t * target = NULL;

    if (!dino_started && !dino_game_over) target = dino_start_hint;
    else if (dino_game_over) target = dino_gameover_hint;

    if (!target) return;
    if (!dino_last_hint_tick) dino_last_hint_tick = now;
    if (now - dino_last_hint_tick < DINO_HINT_BLINK_MS) return;

    dino_last_hint_tick = now;
    dino_hint_toggle = !dino_hint_toggle;
    lv_obj_set_style_text_opa(target, dino_hint_toggle ? 120 : 255, 0);
}

static void _dino_game_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    uint32_t now = lv_tick_get();
    uint32_t dt_ms = dino_last_tick ? (now - dino_last_tick) : 20;
    if (dt_ms > 60) dt_ms = 60;
    dino_last_tick = now;

    _dino_update_hint_blink(now);

    if (!dino_started || dino_game_over) return;

    float dt = (float)dt_ms / 1000.0f;
    int32_t move_x = (int32_t)(dino_speed * dt);
    if (move_x < 1) move_x = 1;

    dino_score += move_x;
    if (dino_score / DINO_SPEED_STEP_SCORE > (dino_speed - DINO_SPEED_INIT) / DINO_SPEED_STEP_VALUE) {
        dino_speed += DINO_SPEED_STEP_VALUE;
        if (dino_speed > DINO_SPEED_MAX) dino_speed = DINO_SPEED_MAX;
    }

    if (dino_in_air) {
        dino_player_vy -= (int32_t)(DINO_GRAVITY * dt);
        dino_player_y -= (int32_t)(dino_player_vy * dt);
        if (dino_player_y >= _dino_ground_top()) {
            dino_player_y = _dino_ground_top();
            dino_player_vy = 0;
            dino_in_air = false;
        }
        if (dino_player_y < 12) dino_player_y = 12;
    } else if ((dino_score / 18) % 2 == 0) {
        dino_leg_toggle = !dino_leg_toggle;
    }

    _dino_update_player_pose();
    _dino_move_ground(move_x);
    _dino_move_clouds(move_x);

    dino_spawn_gap -= move_x;
    if (dino_spawn_gap <= 0) {
        _dino_spawn_cactus();
        dino_spawn_gap = 130 + rand() % 120;
    }

    for (int i = 0; i < DINO_CACTUS_MAX; i++) {
        if (!dino_cactus[i].active) continue;

        dino_cactus[i].x -= move_x;
        if (dino_cactus[i].extra_w > 0) dino_cactus[i].extra_x -= move_x;
        if (dino_cactus[i].x + dino_cactus[i].w < 0) {
            dino_cactus[i].active = false;
            lv_obj_add_flag(dino_cactus[i].obj, LV_OBJ_FLAG_HIDDEN);
            if (dino_cactus[i].extra_obj) lv_obj_add_flag(dino_cactus[i].extra_obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_set_x(dino_cactus[i].obj, dino_cactus[i].x);
        if (dino_cactus[i].extra_w > 0) lv_obj_set_x(dino_cactus[i].extra_obj, dino_cactus[i].extra_x);

        if (_dino_hit(DINO_PLAYER_X + 3, dino_player_y + 4, DINO_PLAYER_W - 6, DINO_PLAYER_H - 6,
                      dino_cactus[i].x, DINO_GROUND_Y - dino_cactus[i].h, dino_cactus[i].w, dino_cactus[i].h)) {
            _dino_finish_game();
            return;
        }

        if (dino_cactus[i].extra_w > 0 &&
            _dino_hit(DINO_PLAYER_X + 3, dino_player_y + 4, DINO_PLAYER_W - 6, DINO_PLAYER_H - 6,
                      dino_cactus[i].extra_x, DINO_GROUND_Y - dino_cactus[i].extra_h,
                      dino_cactus[i].extra_w, dino_cactus[i].extra_h)) {
            _dino_finish_game();
            return;
        }
    }

    _dino_update_hud();
}

static void _dino_press_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (lv_tick_elaps(dino_page_open_tick) < 250) return;

    if (dino_game_over) {
        _dino_reset_world();
        _dino_start_game();
        _dino_jump();
        return;
    }

    if (!dino_started) {
        _dino_start_game();
        _dino_jump();
        return;
    }

    _dino_jump();
}

static void _gesture_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT ||
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
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

    dino_page = lv_obj_create(NULL);
    lv_obj_remove_flag(dino_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(dino_page, lv_color_hex(0xEFEFEF), 0);
    lv_obj_set_style_bg_opa(dino_page, 255, 0);
    lv_obj_set_style_pad_all(dino_page, 0, 0);

    dino_hud_panel = lv_obj_create(dino_page);
    lv_obj_set_size(dino_hud_panel, UI_SCREEN_WIDTH - 12, DINO_TOPBAR_H);
    lv_obj_set_pos(dino_hud_panel, 6, 4);
    lv_obj_remove_flag(dino_hud_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dino_hud_panel, 10, 0);
    lv_obj_set_style_border_width(dino_hud_panel, 0, 0);
    lv_obj_set_style_pad_all(dino_hud_panel, 0, 0);
    lv_obj_set_style_bg_color(dino_hud_panel, lv_color_hex(0xEDEDED), 0);
    lv_obj_set_style_bg_opa(dino_hud_panel, 255, 0);

    lv_obj_t * title_label = lv_label_create(dino_hud_panel);
    lv_obj_set_pos(title_label, 8, 6);
    lv_obj_set_style_text_font(title_label, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x535353), 0);
    lv_label_set_text(title_label, "Dino Run");

    dino_score_label = lv_label_create(dino_hud_panel);
    lv_obj_set_pos(dino_score_label, 86, 6);
    lv_obj_set_style_text_font(dino_score_label, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(dino_score_label, lv_color_hex(0x535353), 0);

    dino_speed_label = lv_label_create(dino_hud_panel);
    lv_obj_set_pos(dino_speed_label, 244, 6);
    lv_obj_set_style_text_font(dino_speed_label, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(dino_speed_label, lv_color_hex(0x7A7A7A), 0);

    dino_sun_obj = lv_image_create(dino_page);
    lv_image_set_src(dino_sun_obj, &ui_img_sun_png);
    lv_image_set_pivot(dino_sun_obj, 0, 0);
    lv_image_set_scale(dino_sun_obj, 256 * 36 / ui_img_sun_png.header.w);
    lv_obj_set_pos(dino_sun_obj, 268, 56);
    lv_obj_set_style_image_opa(dino_sun_obj, 155, 0);

   for (int i = 0; i < DINO_CLOUD_MAX; i++) {
        dino_cloud[i].obj = lv_image_create(dino_page);
        lv_image_set_src(dino_cloud[i].obj, &ui_img_clouds_png);
        lv_image_set_pivot(dino_cloud[i].obj, 0, 0);
        lv_image_set_scale(dino_cloud[i].obj, 256 * 60 / ui_img_clouds_png.header.w);
        lv_obj_set_style_image_recolor(dino_cloud[i].obj, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_image_recolor_opa(dino_cloud[i].obj, 96, 0);
        lv_obj_set_style_image_opa(dino_cloud[i].obj, 170, 0);
        dino_cloud[i].speed_divider = 4 + i;
    }

    lv_obj_t * ground_line = lv_obj_create(dino_page);
    lv_obj_set_size(ground_line, UI_SCREEN_WIDTH, 2);
    lv_obj_set_pos(ground_line, 0, DINO_PLAY_TOP + DINO_GROUND_Y);
    lv_obj_remove_flag(ground_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(ground_line, 0, 0);
    lv_obj_set_style_radius(ground_line, 0, 0);
    lv_obj_set_style_bg_color(ground_line, lv_color_hex(0x535353), 0);
    lv_obj_set_style_bg_opa(ground_line, 255, 0);

    for (int i = 0; i < DINO_GROUND_SEG_MAX; i++) {
        dino_ground_seg[i].obj = lv_obj_create(dino_page);
        dino_ground_seg[i].w = 12 + (i % 3) * 6;
        lv_obj_set_size(dino_ground_seg[i].obj, dino_ground_seg[i].w, 2);
        lv_obj_remove_flag(dino_ground_seg[i].obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(dino_ground_seg[i].obj, 0, 0);
        lv_obj_set_style_radius(dino_ground_seg[i].obj, 0, 0);
        lv_obj_set_style_bg_color(dino_ground_seg[i].obj, lv_color_hex(0x8E8E8E), 0);
        lv_obj_set_style_bg_opa(dino_ground_seg[i].obj, 255, 0);
    }

    dino_player_obj = lv_image_create(dino_page);
    lv_obj_remove_flag(dino_player_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(dino_player_obj, &ui_img_dino_runner_png);

    dino_player_shadow = lv_obj_create(dino_page);
    lv_obj_remove_flag(dino_player_shadow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(dino_player_shadow, 0, 0);
    lv_obj_set_style_radius(dino_player_shadow, 3, 0);
    lv_obj_set_style_bg_color(dino_player_shadow, lv_color_hex(0xA8A8A8), 0);
    lv_obj_set_style_bg_opa(dino_player_shadow, 180, 0);

    dino_start_card = lv_obj_create(dino_page);
    lv_obj_set_size(dino_start_card, 216, 92);
    lv_obj_set_pos(dino_start_card, 52, 86);
    lv_obj_set_style_pad_all(dino_start_card, 0, 0);
    lv_obj_remove_flag(dino_start_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dino_start_card, 14, 0);
    lv_obj_set_style_border_width(dino_start_card, 1, 0);
    lv_obj_set_style_border_color(dino_start_card, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_bg_color(dino_start_card, lv_color_hex(0xFCFCFC), 0);
    lv_obj_set_style_bg_opa(dino_start_card, 252, 0);
    lv_obj_set_style_shadow_width(dino_start_card, 16, 0);
    lv_obj_set_style_shadow_opa(dino_start_card, 32, 0);

    lv_obj_t * start_title = lv_label_create(dino_start_card);
    lv_obj_set_pos(start_title, 0, 12);
    lv_obj_set_width(start_title, lv_pct(100));
    lv_obj_set_style_text_align(start_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(start_title, &ui_font_heiti24, 0);
    lv_obj_set_style_text_color(start_title, lv_color_hex(0x414141), 0);
    lv_label_set_text(start_title, "DINO READY");

    dino_start_hint = lv_label_create(dino_start_card);
    lv_obj_set_pos(dino_start_hint, 0, 45);
    lv_obj_set_width(dino_start_hint, lv_pct(100));
    lv_obj_set_style_text_align(dino_start_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dino_start_hint, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(dino_start_hint, lv_color_hex(0x777777), 0);
    lv_label_set_text(dino_start_hint, "TAP TO JUMP\nSWIPE TO GO BACK");

    dino_gameover_card = lv_obj_create(dino_page);
    lv_obj_set_size(dino_gameover_card, 216, 102);
    lv_obj_set_pos(dino_gameover_card, 52, 82);
    lv_obj_set_style_pad_all(dino_gameover_card, 0, 0);
    lv_obj_remove_flag(dino_gameover_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dino_gameover_card, 14, 0);
    lv_obj_set_style_border_width(dino_gameover_card, 1, 0);
    lv_obj_set_style_border_color(dino_gameover_card, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_bg_color(dino_gameover_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(dino_gameover_card, 250, 0);
    lv_obj_set_style_shadow_width(dino_gameover_card, 18, 0);
    lv_obj_set_style_shadow_opa(dino_gameover_card, 36, 0);

    lv_obj_t * over_title = lv_label_create(dino_gameover_card);
    lv_obj_set_pos(over_title, 0, 12);
    lv_obj_set_width(over_title, lv_pct(100));
    lv_obj_set_style_text_align(over_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(over_title, &ui_font_heiti24, 0);
    lv_obj_set_style_text_color(over_title, lv_color_hex(0x424242), 0);
    lv_label_set_text(over_title, "GAME OVER");

    dino_gameover_score = lv_label_create(dino_gameover_card);
    lv_obj_set_pos(dino_gameover_score, 0, 48);
    lv_obj_set_width(dino_gameover_score, lv_pct(100));
    lv_obj_set_style_text_align(dino_gameover_score, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dino_gameover_score, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(dino_gameover_score, lv_color_hex(0x666666), 0);
    lv_label_set_text(dino_gameover_score, "RUN 0000  BEST 0000");

    dino_gameover_hint = lv_label_create(dino_gameover_card);
    lv_obj_set_pos(dino_gameover_hint, 0, 74);
    lv_obj_set_width(dino_gameover_hint, lv_pct(100));
    lv_obj_set_style_text_align(dino_gameover_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dino_gameover_hint, &ui_font_heiti14, 0);
    lv_obj_set_style_text_color(dino_gameover_hint, lv_color_hex(0x888888), 0);
    lv_label_set_text(dino_gameover_hint, "TAP TO RESTART");

    for (int i = 0; i < DINO_CACTUS_MAX; i++) {
        dino_cactus[i].obj = NULL;
        dino_cactus[i].arm_top = NULL;
        dino_cactus[i].arm_mid = NULL;
        dino_cactus[i].extra_obj = NULL;
        dino_cactus[i].extra_arm_top = NULL;
        dino_cactus[i].extra_arm_mid = NULL;
        dino_cactus[i].active = false;
    }

    lv_obj_add_event_cb(dino_page, _dino_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(dino_start_card, _dino_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(dino_gameover_card, _dino_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(dino_page, _gesture_cb, LV_EVENT_GESTURE, NULL);

    _dino_reset_world();
    dino_timer = lv_timer_create(_dino_game_timer_cb, 20, NULL);

    // #region debug-point A:layout-and-decoder
    if (getenv("DINO_DEBUG_RUN")) {
        lv_obj_update_layout(dino_page);
        lv_obj_t * objects[] = {dino_player_obj, title_label, start_title, dino_start_hint};
        for (unsigned i = 0; i < 4; ++i) {
            lv_area_t area, parent_area;
            lv_point_t pivot = {0, 0};
            lv_obj_get_coords(objects[i], &area);
            lv_obj_get_content_coords(lv_obj_get_parent(objects[i]), &parent_area);
            int scale = 256, decoded = -1;
            if (i == 0) {
                lv_image_get_pivot(objects[i], &pivot);
                scale = lv_image_get_scale_x(objects[i]);
                lv_image_decoder_dsc_t decoder = {0};
                decoded = lv_image_decoder_open(&decoder, &ui_img_dino_runner_png, NULL);
                if (decoded == LV_RESULT_OK) lv_image_decoder_close(&decoder);
            }
            char payload[1024];
            snprintf(payload, sizeof(payload),
                     "{\"sessionId\":\"dino-layout\",\"runId\":\"%s\",\"hypothesisId\":\"%s\","
                     "\"msg\":\"[DEBUG] layout and decode\",\"data\":{\"object\":%u,"
                     "\"bounds\":[%d,%d,%d,%d],\"parentContent\":[%d,%d,%d,%d],"
                     "\"pivot\":[%d,%d],\"scale\":%d,\"decodeResult\":%d}}",
                     getenv("DINO_DEBUG_RUN"), i ? "B" : "A,C", i,
                     area.x1, area.y1, area.x2, area.y2,
                     parent_area.x1, parent_area.y1, parent_area.x2, parent_area.y2,
                     pivot.x, pivot.y, scale, decoded);
            CURL * report = curl_easy_init();
            if (report) {
                curl_easy_setopt(report, CURLOPT_URL, "http://127.0.0.1:7777/event");
                curl_easy_setopt(report, CURLOPT_POSTFIELDS, payload);
                curl_easy_setopt(report, CURLOPT_TIMEOUT_MS, 250L);
                curl_easy_setopt(report, CURLOPT_NOSIGNAL, 1L);
                curl_easy_perform(report);
                curl_easy_cleanup(report);
            }
        }
    }
    // #endregion
    lv_scr_load_anim(dino_page, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);
}

/////////////////// SCREEN deinit ////////////////////

void ui_DinoGamePage_deinit(void)
{
    if (dino_timer) {
        lv_timer_del(dino_timer);
        dino_timer = NULL;
    }

    for (int i = 0; i < DINO_CACTUS_MAX; i++) {
        dino_cactus[i].obj = NULL;
        dino_cactus[i].arm_top = NULL;
        dino_cactus[i].arm_mid = NULL;
        dino_cactus[i].extra_obj = NULL;
        dino_cactus[i].extra_arm_top = NULL;
        dino_cactus[i].extra_arm_mid = NULL;
        dino_cactus[i].active = false;
    }
}
