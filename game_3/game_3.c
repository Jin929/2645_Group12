#include "Game_3.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;

extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern ADC_HandleTypeDef hadc1;
// JOY1_SW is the confirmation button for Player 1, and JOY1 is also the main joystick.
// JOY2_SW is the confirmation button for Player 2.
// BTN1 forces the game to return to the main menu.
// BTN2 is used as the tapping button in the revive mini-game.
#define JOY1_SW_PORT GPIOC
#define JOY1_SW_PIN  GPIO_PIN_3
#define JOY2_SW_PORT GPIOB
#define JOY2_SW_PIN  GPIO_PIN_0
#define BTN1_PORT GPIOC
#define BTN1_PIN  GPIO_PIN_2
#define BTN2_PORT GPIOA
#define BTN2_PIN  GPIO_PIN_8
// Configuration for the second joystick
static Joystick_cfg_t joystick2_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_5,
    .y_channel = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_2CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};
static Joystick_t joystick2_data;
// Use macros to control timing, speed, and game rules.
#define TOOTH_COUNT        15
#define FRAME_MS           30
#define REVIVE_FRAME_MS    8
#define LOADING_MS         6500
#define LOADING_DOT_MS     350
#define COUNTDOWN_MS       3000// 3-second countdown at the beginning
#define DANGER_SHOW_MS     1200
#define PRESS_STEP_MS      420// CPU thinking time to make the behaviour feel more natural
#define CPU_THINK_MS       800
#define CPU_SHOW_MS        1000
#define GAME_OVER_HOLD_MS  800
#define BITE_ANIM_MS       700
// Settings for the revive mini-game
#define RELEASE_ANIM_MS    900
#define REVIVE_AIM_LIMIT_MS    5000//After testing, 5 seconds was found to be a suitable duration.
#define REVIVE_PRESS_LIMIT_MS  11000
#define REVIVE_REQUIRED_PRESS  12
#define REVIVE_HIT_RADIUS      12
// Press the button 12 times within 11 seconds.
// This frequency is suitable and does not cause much difficulty
// for either the player or the hardware.
#define REVIVE_CURSOR_STEP     4
#define BTN2_HIT_INTERVAL_MS   90
#define REVIVE_MUSIC_AIM_STEP_MS    150
#define REVIVE_MUSIC_HIT_STEP_MS    110
#define REVIVE_MUSIC_TONE_MS        75
// Colour definitions.
#define BLACK   0
#define WHITE   1
#define RED     2
#define GREEN   3
#define BLUE    4
#define ORANGE  5
#define YELLOW  6
#define PINK    7
#define PURPLE  8
#define NAVY    9
#define GOLD    10
#define VIOLET  11
#define BROWN   12
#define GREY    13
#define CYAN    14
#define MAG     15
// Single-player and two-player modes
typedef enum {
    MODE_SINGLE = 0,
    MODE_TWO_PLAYER
} SharkMode;
//FSM
typedef enum {
    ST_LOADING = 0,
    ST_MODE_MENU,
    ST_COUNTDOWN,
    ST_SHOW_DANGER,
    ST_P1_SELECT,
    ST_P1_ACTION,
    ST_CPU_THINK,
    ST_CPU_SHOW,
    ST_CPU_ACTION,
    ST_P2_SELECT,
    ST_P2_ACTION,
    ST_BITE,
    ST_REVIVE,
    ST_RELEASE,
    ST_GAME_OVER
} SharkState;
// Game results
typedef enum {
    RES_NONE = 0,
    RES_P1_LOSE,
    RES_P2_LOSE,
    RES_CPU_LOSE,
    RES_DRAW
} SharkResult;
// Structure for Game 3.
typedef struct {
    SharkMode mode;
    SharkState state;
    SharkResult result;

    uint8_t menu_choice;
    uint8_t danger_tooth;
    uint8_t next_tooth;
    uint8_t tooth_pressed[TOOTH_COUNT];
    uint8_t p1_choice;
    uint8_t p2_choice;
    uint8_t cpu_choice;
    uint8_t action_total;
    uint8_t action_done;
    uint8_t actor;
    uint8_t bite_actor;
    uint32_t state_tick;
    uint32_t press_tick;
    uint32_t render_tick;
    uint8_t joy1_left_lock;
    uint8_t joy1_right_lock;
    uint8_t joy1_up_lock;
    uint8_t joy1_down_lock;
    uint8_t joy1_sw_lock;
    uint8_t joy2_left_lock;
    uint8_t joy2_right_lock;
    uint8_t joy2_sw_lock;
    uint8_t revive_player;
    uint8_t revive_target_x;
    uint8_t revive_target_y;
    uint8_t revive_cursor_x;
    uint8_t revive_cursor_y;
    uint8_t revive_aligned;
    uint8_t revive_press_count;
    uint8_t btn1_lock;
    uint8_t btn2_lock;
    uint32_t btn2_hit_tick;
} SharkGame;

static SharkGame g;
// X coordinates of the shark teeth
static const uint8_t tooth_x[TOOTH_COUNT] = {
    72, 86, 100, 114, 128, 142, 156, 170,
    78, 92, 106, 120, 134, 148, 162
};
static void reset_round(void);

static void sw_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pin = JOY1_SW_PIN;
    HAL_GPIO_Init(JOY1_SW_PORT, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = JOY2_SW_PIN;
    HAL_GPIO_Init(JOY2_SW_PORT, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = BTN1_PIN;
    HAL_GPIO_Init(BTN1_PORT, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = BTN2_PIN;
    HAL_GPIO_Init(BTN2_PORT, &GPIO_InitStruct);
}
// Pin reading.
static uint8_t pin_pressed(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}
// Decide whether to read the confirmation button of Player 1 or Player 2
static uint8_t sw_down(uint8_t joy_id)
{
    if (joy_id == 2U) {
        return pin_pressed(JOY2_SW_PORT, JOY2_SW_PIN);
    }

    return pin_pressed(JOY1_SW_PORT, JOY1_SW_PIN);
}

static uint8_t confirm_pressed(uint8_t joy_id)
{
    uint8_t down = sw_down(joy_id);
    if (joy_id == 2U) {
        if (down && !g.joy2_sw_lock) {
            g.joy2_sw_lock = 1U;
            return 1U;
        }
        if (!down) {
            g.joy2_sw_lock = 0U;
        }
    } else {
        if (down && !g.joy1_sw_lock) {
            g.joy1_sw_lock = 1U;
            return 1U;
        }
        if (!down) {
            g.joy1_sw_lock = 0U;
        }
    }
    return 0U;
}

static uint8_t btn1_down(void)
{
    return pin_pressed(BTN1_PORT, BTN1_PIN);
}

static uint8_t btn2_down(void)
{
    return pin_pressed(BTN2_PORT, BTN2_PIN);
}

static uint8_t btn1_pressed(void)
{
    uint8_t down = btn1_down();
    if (down && !g.btn1_lock) {
        g.btn1_lock = 1U;
        return 1U;
    }
    if (!down) {
        g.btn1_lock = 0U;
    }
    return 0U;
}

static uint8_t btn2_pressed(void)
{
    uint8_t down = btn2_down();
    if (down && !g.btn2_lock) {
        g.btn2_lock = 1U;
        return 1U;
    }
    if (!down) {
        g.btn2_lock = 0U;
    }
    return 0U;
}

static uint8_t btn2_hit_sensitive(void)
{
    uint32_t now = HAL_GetTick();
    if (btn2_down()) {
        if ((now - g.btn2_hit_tick) >= BTN2_HIT_INTERVAL_MS) {
            g.btn2_hit_tick = now;
            return 1U;
        }
    } else {
        g.btn2_lock = 0U;
    }
    return 0U;
}
// Read joystick directions
static UserInput read_joystick(uint8_t joy_id)
{
    if (joy_id == 2U) {
        Joystick_Read(&joystick2_cfg, &joystick2_data);
        return Joystick_GetInput(&joystick2_data);
    }
    Joystick_Read(&joystick_cfg, &joystick_data);
    return Joystick_GetInput(&joystick_data);
}
static uint8_t joy_left(uint8_t joy_id)
{
    UserInput input = read_joystick(joy_id);
    return (input.direction == W || input.direction == NW || input.direction == SW);
}
static uint8_t joy_right(uint8_t joy_id)
{
    UserInput input = read_joystick(joy_id);
    return (input.direction == E || input.direction == NE || input.direction == SE);
}
static uint8_t joy_up(uint8_t joy_id)
{
    UserInput input = read_joystick(joy_id);
    return (input.direction == N || input.direction == NE || input.direction == NW);
}
static uint8_t joy_down(uint8_t joy_id)
{
    UserInput input = read_joystick(joy_id);
    return (input.direction == S || input.direction == SE || input.direction == SW);
}
static uint8_t remaining_teeth(void)
{
    if (g.next_tooth >= TOOTH_COUNT) {
        return 0U;
    }
    return (uint8_t)(TOOTH_COUNT - g.next_tooth);
}
// Ensure that the selectable number of teeth does not exceed the number of remaining teeth.
static uint8_t max_choice(void)
{
    uint8_t r = remaining_teeth();
    return (r >= 3U) ? 3U : r;
}
static uint8_t random_1_to(uint8_t maxv)
{
    if (maxv == 0U) {
        return 0U;
    }

    return (uint8_t)((HAL_GetTick() % maxv) + 1U);
}
static void beep(uint16_t freq, uint16_t ms)
{
    buzzer_tone(&buzzer_cfg, freq, 30);
    HAL_Delay(ms);
    buzzer_off(&buzzer_cfg);
}
// Centre the text.
static void center_text(const char *s, int y, uint8_t col, uint8_t size)
{
    int len = 0;
    int x;
    while (s[len] != '\0') {
        len++;
    }
    x = (240 - len * 6 * size) / 2;
    if (x < 0) {
        x = 0;
    }

    LCD_printString(s, (uint16_t)x, (uint16_t)y, col, size);
}
// Make the text bold so that it is easier to read.
static void print_bold(const char *s, int x, int y, uint8_t col, uint8_t size)
{
    LCD_printString(s, (uint16_t)x, (uint16_t)y, col, size);
    LCD_printString(s, (uint16_t)(x + 1), (uint16_t)y, col, size);
}
// Make the text bold and centred, mainly for titles and important information.
static void center_text_bold(const char *s, int y, uint8_t col, uint8_t size)
{
    int len = 0;
    int x;
    while (s[len] != '\0') {
        len++;
    }
    x = (240 - len * 6 * size) / 2;
    if (x < 0) {
        x = 0;
    }
    print_bold(s, x, y, col, size);
}
// Used to draw the shark.
static void fill_ellipse(int cx, int cy, int rx, int ry, uint8_t col)
{
    long rx2 = (long)rx * rx;
    long ry2 = (long)ry * ry;
    long rhs = rx2 * ry2;
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            long lhs = (long)x * x * ry2 + (long)y * y * rx2;
            int px = cx + x;
            int py = cy + y;
            if (lhs <= rhs && px >= 0 && px < 240 && py >= 0 && py < 240) {
                LCD_Set_Pixel((uint16_t)px, (uint16_t)py, col);
            }
        }
    }
}
// Used to draw the shark.
static void fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t col)
{
    int minx = x1;
    int maxx = x1;
    int miny = y1;
    int maxy = y1;
    if (x2 < minx) minx = x2;
    if (x3 < minx) minx = x3;
    if (x2 > maxx) maxx = x2;
    if (x3 > maxx) maxx = x3;
    if (y2 < miny) miny = y2;
    if (y3 < miny) miny = y3;
    if (y2 > maxy) maxy = y2;
    if (y3 > maxy) maxy = y3;
    long d = (long)(y2 - y3) * (x1 - x3) + (long)(x3 - x2) * (y1 - y3);
    if (d == 0) {
        return;
    }
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            long a = (long)(y2 - y3) * (x - x3) + (long)(x3 - x2) * (y - y3);
            long b = (long)(y3 - y1) * (x - x3) + (long)(x1 - x3) * (y - y3);
            long c = d - a - b;

            if (((a >= 0 && b >= 0 && c >= 0 && d > 0) ||
                 (a <= 0 && b <= 0 && c <= 0 && d < 0)) &&
                x >= 0 && x < 240 && y >= 0 && y < 240) {
                LCD_Set_Pixel((uint16_t)x, (uint16_t)y, col);
            }
        }
    }
}
/* ================= Loading screen drawing section =================*/
/** 
* After Game 3 is selected, this loading screen is displayed first.
 * The loading screen is mainly used to introduce the rules, and it also includes a loading animation to make the interface less boring.
 */
void draw_loading_small_shark(int cx, int cy)
{
    /* Draw the small shark using basic geometric shapes. */
    fill_ellipse(cx, cy, 19, 9, GREY);                         /* body */
    fill_triangle(cx - 16, cy, cx - 30, cy - 9, cx - 29, cy + 9, GREY);   /* tail */
    fill_triangle(cx - 23, cy - 1, cx - 34, cy - 13, cx - 32, cy - 3, GREY);
    fill_triangle(cx - 23, cy + 1, cx - 34, cy + 13, cx - 32, cy + 3, GREY);
    fill_triangle(cx - 3, cy - 8, cx + 5, cy - 22, cx + 13, cy - 8, GREY); /* dorsal fin */
    fill_triangle(cx - 3, cy + 7, cx - 11, cy + 18, cx + 8, cy + 9, GREY); /* lower fin */
    fill_triangle(cx + 14, cy, cx + 31, cy - 6, cx + 31, cy + 6, GREY);    /* nose */
    fill_ellipse(cx + 6, cy + 3, 15, 5, WHITE);                /* belly */
    LCD_Draw_Circle((uint16_t)(cx + 13), (uint16_t)(cy - 4), 3, WHITE, 1);
    LCD_Draw_Circle((uint16_t)(cx + 14), (uint16_t)(cy - 4), 1, BLACK, 1);
    LCD_Draw_Line((uint16_t)(cx + 5), (uint16_t)(cy + 4),
                  (uint16_t)(cx + 18), (uint16_t)(cy + 3), BROWN);
    LCD_Draw_Line((uint16_t)(cx + 20), (uint16_t)(cy + 1),
                  (uint16_t)(cx + 24), (uint16_t)(cy + 2), BROWN);
}
 
static void draw_loading_screen(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - g.state_tick;
    uint16_t bar_progress;
    uint8_t dot_step;
    int bar_x = 30;
    int bar_y = 76;
    int bar_w = 180;
    int shark_x;
    char percent_text[8];
    if (elapsed > LOADING_MS) {
        elapsed = LOADING_MS;
    }
    bar_progress = (uint16_t)((elapsed * 176U) / LOADING_MS);
    dot_step = (uint8_t)((now / LOADING_DOT_MS) % 4U);
    shark_x = bar_x + 8 + (int)bar_progress;
    // Warm and simple background.
    LCD_Fill_Buffer(BROWN);
    LCD_Draw_Rect(5, 5, 230, 230, GOLD, 0);
    LCD_Draw_Rect(10, 10, 220, 220, ORANGE, 0);
    LCD_Draw_Rect(15, 15, 210, 210, GOLD, 0);
    // Background decorative elements.
    fill_ellipse(38, 35, 22, 5, ORANGE);
    fill_ellipse(201, 35, 22, 5, GOLD);
    LCD_Draw_Circle(28, 58, 3, WHITE, 0);
    LCD_Draw_Circle(42, 66, 2, GOLD, 1);
    LCD_Draw_Circle(211, 59, 3, WHITE, 0);
    LCD_Draw_Circle(198, 67, 2, ORANGE, 1);
    center_text_bold("SHARK BITE", 22, GOLD, 2);
    // The loading text uses a simple animation effect.
    if (dot_step == 0U) {
        center_text_bold("Loading", 48, WHITE, 2);
    } else if (dot_step == 1U) {
        center_text_bold("Loading.", 48, WHITE, 2);
    } else if (dot_step == 2U) {
        center_text_bold("Loading..", 48, WHITE, 2);
    } else {
        center_text_bold("Loading...", 48, WHITE, 2);
    }
    // Progress bar: as loading time increases, the bar fills more and the small shark moves further to the right. 
    LCD_Draw_Rect((uint16_t)bar_x, (uint16_t)bar_y, (uint16_t)bar_w, 14, WHITE, 0);
    LCD_Draw_Rect((uint16_t)(bar_x + 2), (uint16_t)(bar_y + 2), (uint16_t)(bar_w - 4), 10, ORANGE, 0);
    if (bar_progress > 0U) {
        LCD_Draw_Rect((uint16_t)(bar_x + 2), (uint16_t)(bar_y + 2), bar_progress, 10, GOLD, 1);
    }
    LCD_Draw_Line((uint16_t)(bar_x + 4), (uint16_t)(bar_y + 5),
                  (uint16_t)(bar_x + bar_progress), (uint16_t)(bar_y + 5), YELLOW);
    if (shark_x < 38) {
        shark_x = 38;
    }
    if (shark_x > 202) {
        shark_x = 202;
    }
    draw_loading_small_shark(shark_x, 70);
    sprintf(percent_text, "%lu%%", (unsigned long)((elapsed * 100U) / LOADING_MS));
    print_bold(percent_text, 170, 96, WHITE, 2);
    // Rules. 
    center_text_bold("How to Play", 116, GOLD, 1);
    print_bold("Single: P1 vs CPU", 36, 138, WHITE, 1);
    print_bold("Two-player: P1 vs P2", 36, 153, WHITE, 1);
    print_bold("Choose 1-3 teeth each turn", 36, 168, WHITE, 1);
    print_bold("Red tooth bites the loser", 36, 183, WHITE, 1);
    print_bold("Revive: move to target dot", 36, 201, GOLD, 1);
    print_bold("then press BTN2 quickly", 36, 216, GOLD, 1);
    LCD_Refresh(&cfg0);
}
/*====================== Main game =================*/
static void draw_background(void)
{
    LCD_Fill_Buffer(BROWN);
    LCD_Draw_Rect(5, 5, 230, 230, GOLD, 0);
    LCD_Draw_Rect(9, 9, 222, 222, ORANGE, 0);
    fill_ellipse(38, 210, 28, 7, ORANGE);
    fill_ellipse(200, 36, 22, 6, ORANGE);
    fill_ellipse(214, 205, 18, 5, GOLD);
    LCD_Draw_Circle(28, 38, 3, GOLD, 1);
    LCD_Draw_Circle(46, 56, 2, ORANGE, 1);
    LCD_Draw_Circle(206, 72, 3, GOLD, 1);

    center_text("SHARK BITE", 19, GOLD, 2);
}

static void draw_badge(const char *s)
{
    LCD_Draw_Rect(164, 47, 58, 22, BLACK, 1);
    LCD_Draw_Rect(164, 47, 58, 22, GOLD, 0);
    LCD_printString(s, 178, 54, WHITE, 1);
}

static void draw_tooth(uint8_t index, uint8_t show_danger)
{
    uint8_t col = WHITE;
    int x = tooth_x[index];
    if (g.tooth_pressed[index]) {
        col = GREY;
    } else if (show_danger && index == g.danger_tooth) {
        col = RED;
    }
    if (index < 8U) {
        fill_triangle(x - 5, 121, x + 5, 121, x, 148, col);
        if (!g.tooth_pressed[index]) {
            LCD_Draw_Line(x - 5, 121, x, 148, GREY);
            LCD_Draw_Line(x + 5, 121, x, 148, GREY);
        }
        if (index == g.next_tooth &&
            g.state != ST_GAME_OVER &&
            g.state != ST_REVIVE &&
            g.state != ST_BITE &&
            g.state != ST_RELEASE) {
            LCD_printString("^", x - 3, 108, GOLD, 1);
        }
    } else {
        fill_triangle(x - 5, 178, x + 5, 178, x, 151, col);

        if (!g.tooth_pressed[index]) {
            LCD_Draw_Line(x - 5, 178, x, 151, GREY);
            LCD_Draw_Line(x + 5, 178, x, 151, GREY);
        }
        if (index == g.next_tooth &&
            g.state != ST_GAME_OVER &&
            g.state != ST_REVIVE &&
            g.state != ST_BITE &&
            g.state != ST_RELEASE) {
            LCD_printString("v", x - 3, 183, GOLD, 1);
        }
    }
}
static void draw_teeth(uint8_t show_danger)
{
    for (uint8_t i = 0; i < TOOTH_COUNT; i++) {
        draw_tooth(i, show_danger);
    }
}
// Shark drawing and mouth-opening animation
static void draw_shark(uint8_t mouth_open, uint8_t show_danger)
{
    int mouth_ry;
    fill_triangle(120, 42, 52, 122, 188, 122, GREY);
    fill_ellipse(120, 118, 78, 62, GREY);
    fill_triangle(120, 48, 74, 124, 166, 124, WHITE);
    fill_ellipse(120, 137, 66, 45, WHITE);
    fill_triangle(64, 118, 30, 100, 48, 146, GREY);
    fill_triangle(176, 118, 210, 100, 192, 146, GREY);
    LCD_Draw_Line(66, 126, 78, 133, BLACK);
    LCD_Draw_Line(62, 138, 76, 143, BLACK);
    LCD_Draw_Line(174, 133, 186, 126, BLACK);
    LCD_Draw_Line(178, 143, 192, 138, BLACK);
    LCD_Draw_Circle(88, 104, 10, WHITE, 1);
    LCD_Draw_Circle(152, 104, 10, WHITE, 1);
    LCD_Draw_Circle(91, 106, 4, BLACK, 1);
    LCD_Draw_Circle(149, 106, 4, BLACK, 1);
    LCD_Draw_Line(78, 92, 98, 96, BLACK);
    LCD_Draw_Line(142, 96, 162, 92, BLACK);
    if (mouth_open == 0U) {
        fill_ellipse(120, 153, 56, 7, BLACK);
        LCD_Draw_Line(72, 153, 168, 153, PINK);
        return;
    }
    if (mouth_open == 1U) {
        mouth_ry = 18;
    } else if (mouth_open == 2U) {
        mouth_ry = 30;
    } else {
        mouth_ry = 42;
    }
    fill_ellipse(120, 151, 68, mouth_ry, PINK);
    fill_ellipse(120, 151, 60, mouth_ry - 7, BLACK);
    LCD_Draw_Line(58, 122, 182, 122, PINK);
    LCD_Draw_Line(64, 180, 176, 180, PINK);
    if (mouth_open >= 2U) {
        draw_teeth(show_danger);
    }
    fill_ellipse(96, 70, 9, 4, WHITE);
    fill_ellipse(144, 70, 9, 4, WHITE);
}

/*==================== Mode selection page ============*/
static void draw_mode_menu(void)
{
    LCD_Fill_Buffer(BROWN);
    LCD_Draw_Rect(5, 5, 230, 230, GOLD, 0);
    LCD_Draw_Rect(9, 9, 222, 222, ORANGE, 0);
    fill_ellipse(40, 214, 28, 7, ORANGE);
    fill_ellipse(202, 34, 24, 6, GOLD);
    LCD_Draw_Circle(34, 50, 3, GOLD, 1);
    LCD_Draw_Circle(204, 70, 2, ORANGE, 1);
    center_text("SHARK BITE", 34, GOLD, 2);
    center_text("Select Mode", 72, WHITE, 1);
    LCD_Draw_Rect(34, 102, 172, 34, (g.menu_choice == 0U) ? GOLD : BLACK, 1);
    LCD_Draw_Rect(34, 102, 172, 34, WHITE, 0);
    center_text("Single Player", 113, WHITE, 1);
    LCD_Draw_Rect(34, 146, 172, 34, (g.menu_choice == 1U) ? GOLD : BLACK, 1);
    LCD_Draw_Rect(34, 146, 172, 34, WHITE, 0);
    center_text("Two Players", 157, WHITE, 1);
    center_text("JOY1 Up/Down", 196, ORANGE, 1);
    center_text("Press to Start", 213, ORANGE, 1);
}

static void draw_status(void)
{
    char buf[20];
    LCD_Draw_Rect(16, 47, 68, 22, BLACK, 1);
    LCD_Draw_Rect(16, 47, 68, 22, GOLD, 0);
    sprintf(buf, "Next:%02u", (unsigned)(g.next_tooth + 1U));
    LCD_printString(buf, 22, 54, WHITE, 1);
    if (g.state == ST_P1_SELECT || g.state == ST_P1_ACTION) {
        draw_badge("P1");
    } else if (g.state == ST_P2_SELECT || g.state == ST_P2_ACTION) {
        draw_badge("P2");
    } else if (g.state == ST_CPU_THINK || g.state == ST_CPU_SHOW || g.state == ST_CPU_ACTION) {
        draw_badge("CPU");
    } else if (g.state == ST_REVIVE) {
        draw_badge("HELP");
    } else if (g.state == ST_BITE) {
        draw_badge("BITE");
    } else {
        draw_badge("GAME");
    }
}
// Selection cards: press 1, 2, or 3 teeth
static void draw_choice_cards(uint8_t choice)
{
    for (uint8_t i = 1; i <= 3; i++) {
        int x = 48 + (i - 1) * 50;
        uint8_t fill = (choice == i) ? GOLD : BLACK;
        if (i > max_choice()) {
            fill = GREY;
        }
        LCD_Draw_Rect(x, 196, 38, 28, fill, 1);
        LCD_Draw_Rect(x, 196, 38, 28, ORANGE, 0);
        if (i <= max_choice()) {
            char s[2] = {(char)('0' + i), '\0'};
            LCD_printString(s, x + 13, 203, WHITE, 2);
        }
    }
}

static uint8_t current_mouth_open(void)
{
    if (g.state == ST_COUNTDOWN) {
        uint32_t e = HAL_GetTick() - g.state_tick;
        if (e < 1000U) return 0U;
        if (e < 2000U) return 1U;
        return 2U;
    }
    return 3U;
}
/*=================== Revive mini-game ==============*/
// Target detection
static uint8_t revive_target_hit(void)
{
    int dx = (int)g.revive_cursor_x - (int)g.revive_target_x;
    int dy = (int)g.revive_cursor_y - (int)g.revive_target_y;
    int limit = REVIVE_HIT_RADIUS * REVIVE_HIT_RADIUS;
    return ((dx * dx + dy * dy) <= limit) ? 1U : 0U;
}
// Use the joystick to control the red dot movement
static void update_revive_cursor(void)
{
    UserInput input = read_joystick(g.revive_player);
    switch (input.direction) {
    case N:
        if (g.revive_cursor_y > 58U) g.revive_cursor_y -= REVIVE_CURSOR_STEP;
        break;
    case S:
        if (g.revive_cursor_y < 178U) g.revive_cursor_y += REVIVE_CURSOR_STEP;
        break;
    case W:
        if (g.revive_cursor_x > 52U) g.revive_cursor_x -= REVIVE_CURSOR_STEP;
        break;
    case E:
        if (g.revive_cursor_x < 188U) g.revive_cursor_x += REVIVE_CURSOR_STEP;
        break;
    case NE:
        if (g.revive_cursor_y > 58U) g.revive_cursor_y -= REVIVE_CURSOR_STEP;
        if (g.revive_cursor_x < 188U) g.revive_cursor_x += REVIVE_CURSOR_STEP;
        break;
    case NW:
        if (g.revive_cursor_y > 58U) g.revive_cursor_y -= REVIVE_CURSOR_STEP;
        if (g.revive_cursor_x > 52U) g.revive_cursor_x -= REVIVE_CURSOR_STEP;
        break;
    case SE:
        if (g.revive_cursor_y < 178U) g.revive_cursor_y += REVIVE_CURSOR_STEP;
        if (g.revive_cursor_x < 188U) g.revive_cursor_x += REVIVE_CURSOR_STEP;
        break;
    case SW:
        if (g.revive_cursor_y < 178U) g.revive_cursor_y += REVIVE_CURSOR_STEP;
        if (g.revive_cursor_x > 52U) g.revive_cursor_x -= REVIVE_CURSOR_STEP;
        break;
    default:
        break;
    }
}
// Bite-down animation.
static void draw_bite_screen(void)
{
    LCD_Fill_Buffer(BROWN);
    LCD_Draw_Rect(5, 5, 230, 230, GOLD, 0);
    LCD_Draw_Rect(9, 9, 222, 222, ORANGE, 0);
    center_text("DANGER!", 26, RED, 2);
    draw_shark(0U, 1U);
    center_text("The shark bites down!", 198, RED, 1);
    LCD_Refresh(&cfg0);
}
// Mouth-opening animation
static void draw_release_screen(void)
{
    uint32_t elapsed = HAL_GetTick() - g.state_tick;
    uint8_t open_level = 1U;
    if (elapsed > 250U) open_level = 2U;
    if (elapsed > 550U) open_level = 3U;
    draw_background();
    draw_shark(open_level, 0U);
    center_text("Shark released!", 198, GREEN, 1);
    LCD_Refresh(&cfg0);
}
// Revive mini-game screen
static void draw_revive_screen(void)
{
    char buf[32];
    uint32_t now = HAL_GetTick();
    uint32_t left_ms;
    draw_background();
    draw_shark(0U, 0U);
    if (g.revive_player == 1U) {
        center_text("P1 REVIVE", 44, GOLD, 1);
    } else {
        center_text("P2 REVIVE", 44, GOLD, 1);
    }
    if (!g.revive_aligned) {
        uint32_t elapsed = now - g.state_tick;
        left_ms = (elapsed >= REVIVE_AIM_LIMIT_MS) ? 0U : (REVIVE_AIM_LIMIT_MS - elapsed);
        sprintf(buf, "Aim:%lu.%lus", left_ms / 1000U, (left_ms % 1000U) / 100U);
        LCD_printString(buf, 16, 218, ORANGE, 1);
        LCD_printString("Aim red dot", 138, 218, ORANGE, 1);
    } else {
        uint32_t elapsed = now - g.press_tick;
        left_ms = (elapsed >= REVIVE_PRESS_LIMIT_MS) ? 0U : (REVIVE_PRESS_LIMIT_MS - elapsed);
        sprintf(buf, "Hit:%u/%u", g.revive_press_count, REVIVE_REQUIRED_PRESS);
        LCD_printString(buf, 16, 218, ORANGE, 1);
        sprintf(buf, "Time:%lu.%lus", left_ms / 1000U, (left_ms % 1000U) / 100U);
        LCD_printString(buf, 130, 218, ORANGE, 1);
    }
    LCD_Draw_Circle(g.revive_target_x, g.revive_target_y, 18, WHITE, 0);
    LCD_Draw_Circle(g.revive_target_x, g.revive_target_y, 4, GOLD, 1);
    LCD_Draw_Circle(g.revive_cursor_x, g.revive_cursor_y, 8, RED, 1);
    LCD_Draw_Circle(g.revive_cursor_x, g.revive_cursor_y, 8, WHITE, 0);
    if (g.revive_aligned) {
        center_text("Press BTN2 to hit!", 198, GREEN, 1);
    } else {
        center_text("Move red dot to circle", 198, WHITE, 1);
    }
    LCD_Refresh(&cfg0);
}
/*============= Game-over screen ==============*/
// Include different endings for single-player and two-player modes
static void draw_game_over_screen(void)
{
    LCD_Fill_Buffer(BROWN);
    LCD_Draw_Rect(5, 5, 230, 230, GOLD, 0);
    LCD_Draw_Rect(9, 9, 222, 222, ORANGE, 0);
    center_text("RESULT", 28, GOLD, 3);
    LCD_Draw_Rect(24, 86, 192, 58, BLACK, 1);
    LCD_Draw_Rect(24, 86, 192, 58, GOLD, 0);
    if (g.result == RES_DRAW) {
        center_text("DRAW", 98, GREEN, 3);
        center_text("Both players survived", 132, WHITE, 1);
    } else if (g.mode == MODE_SINGLE) {
        if (g.result == RES_CPU_LOSE) {
            center_text("YOU WIN", 104, GREEN, 2);
        } else {
            center_text("YOU LOSE", 104, RED, 2);
        }
    } else {
        if (g.result == RES_P1_LOSE) {
            center_text("P1 LOSE", 98, RED, 2);
            center_text("P2 WIN", 124, GREEN, 2);
        } else if (g.result == RES_P2_LOSE) {
            center_text("P2 LOSE", 98, RED, 2);
            center_text("P1 WIN", 124, GREEN, 2);
        } else {
            center_text("GAME OVER", 106, WHITE, 2);
        }
    }
    center_text("JOY1: Play Again", 178, ORANGE, 1);
    center_text("BTN1: Main Menu", 198, ORANGE, 1);
    LCD_Refresh(&cfg0);
}

static void render(void)
{
    char buf[32];
    uint8_t show_danger = 0U;
    if (g.state == ST_LOADING) {
        draw_loading_screen();
        return;
    }
    if (g.state == ST_MODE_MENU) {
        draw_mode_menu();
        LCD_Refresh(&cfg0);
        return;
    }
    if (g.state == ST_BITE) {
        draw_bite_screen();
        return;
    }
    if (g.state == ST_REVIVE) {
        draw_revive_screen();
        return;
    }
    if (g.state == ST_RELEASE) {
        draw_release_screen();
        return;
    }
    if (g.state == ST_GAME_OVER) {
        draw_game_over_screen();
        return;
    }
    if (g.state == ST_SHOW_DANGER) {
        show_danger = 1U;
    }
    draw_background();
    draw_status();
    draw_shark(current_mouth_open(), show_danger);
    switch (g.state) {
    case ST_COUNTDOWN:
    {
        uint32_t e = HAL_GetTick() - g.state_tick;
        uint8_t n = (e < 1000U) ? 3U : ((e < 2000U) ? 2U : 1U);
        char s[2] = {(char)('0' + n), '\0'};
        center_text(s, 198, GOLD, 3);
        break;
    }
    case ST_SHOW_DANGER:
        center_text("Remember the red tooth", 204, GOLD, 1);
        break;
    case ST_P1_SELECT:
        center_text("P1: choose 1-3 teeth", 188, ORANGE, 1);
        draw_choice_cards(g.p1_choice);
        break;
    case ST_P2_SELECT:
        center_text("P2: choose 1-3 teeth", 188, ORANGE, 1);
        draw_choice_cards(g.p2_choice);
        break;
    case ST_CPU_THINK:
        center_text("CPU is thinking...", 204, ORANGE, 1);
        break;
    case ST_CPU_SHOW:
        sprintf(buf, "CPU chose %u", (unsigned)g.cpu_choice);
        center_text(buf, 204, GOLD, 1);
        break;
    case ST_P1_ACTION:
        sprintf(buf, "P1 pressing %u", (unsigned)g.action_total);
        center_text(buf, 204, ORANGE, 1);
        break;
    case ST_P2_ACTION:
        sprintf(buf, "P2 pressing %u", (unsigned)g.action_total);
        center_text(buf, 204, ORANGE, 1);
        break;
    case ST_CPU_ACTION:
        sprintf(buf, "CPU pressing %u", (unsigned)g.action_total);
        center_text(buf, 204, ORANGE, 1);
        break;
    default:
        break;
    }
    LCD_Refresh(&cfg0);
}
// Reset variables when a new round starts
static void reset_round(void)
{
    for (uint8_t i = 0; i < TOOTH_COUNT; i++) {
        g.tooth_pressed[i] = 0U;
    }
    g.result = RES_NONE;
    g.danger_tooth = (uint8_t)(10U + (HAL_GetTick() % 5U));
    g.next_tooth = 0U;
    g.p1_choice = 1U;
    g.p2_choice = 1U;
    g.cpu_choice = 1U;
    g.action_total = 0U;
    g.action_done = 0U;
    g.actor = 0U;
    g.bite_actor = 0U;
    g.state_tick = HAL_GetTick();
    g.press_tick = HAL_GetTick();
    g.render_tick = HAL_GetTick();
    g.joy1_left_lock = 0U;
    g.joy1_right_lock = 0U;
    g.joy1_up_lock = 0U;
    g.joy1_down_lock = 0U;
    g.joy2_left_lock = 0U;
    g.joy2_right_lock = 0U;
    g.joy1_sw_lock = sw_down(1U);
    g.joy2_sw_lock = sw_down(2U);
    g.revive_player = 0U;
    g.revive_target_x = 120U;
    g.revive_target_y = 110U;
    g.revive_cursor_x = 120U;
    g.revive_cursor_y = 150U;
    g.revive_aligned = 0U;
    g.revive_press_count = 0U;
    g.btn1_lock = btn1_down();
    g.btn2_lock = btn2_down();
    g.btn2_hit_tick = HAL_GetTick();
}

static void shark_init(void)
{
    LCD_Set_Palette(PALETTE_DEFAULT);
    PWM_SetDuty(&pwm_cfg, 20);
    buzzer_off(&buzzer_cfg);
    sw_gpio_init();
    Joystick_Init(&joystick2_cfg);
    Joystick_Calibrate(&joystick2_cfg);
    g.mode = MODE_SINGLE;
    g.menu_choice = 0U;
    g.state = ST_LOADING;
    reset_round();
    g.state = ST_LOADING;
    g.state_tick = HAL_GetTick();
}

static void begin_action(uint8_t count, uint8_t actor)
{
    uint8_t r = remaining_teeth();
    if (count > r) {
        count = r;
    }
    g.action_total = count;
    g.action_done = 0U;
    g.actor = actor;
    g.press_tick = HAL_GetTick();
    g.state_tick = HAL_GetTick();
}

static void start_bite(uint8_t actor)
{
    g.bite_actor = actor;
    g.state = ST_BITE;
    g.state_tick = HAL_GetTick();
    buzzer_tone(&buzzer_cfg, 230, 30);
}

static void start_revive(uint8_t loser)
{
    uint32_t t = HAL_GetTick();
    g.revive_player = loser;
    g.revive_target_x = (uint8_t)(70U + ((t * 37U) % 100U));
    g.revive_target_y = (uint8_t)(72U + ((t * 53U) % 78U));
    g.revive_cursor_x = 120U;
    g.revive_cursor_y = 158U;
    g.revive_aligned = 0U;
    g.revive_press_count = 0U;
    g.btn2_lock = btn2_down();
    g.btn2_hit_tick = HAL_GetTick();
    g.state = ST_REVIVE;
    g.state_tick = HAL_GetTick();
    g.press_tick = 0U;
    g.render_tick = 0U;
    buzzer_off(&buzzer_cfg);
}
// Revive music: different music is played when using the joystick and when pressing the button
static void revive_music_update(void)
{
    static uint8_t last_phase = 255U;
    static uint8_t step = 0U;
    static uint32_t step_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint8_t phase = g.revive_aligned;
    const uint16_t aim_notes[] = {784, 988, 1175, 988};
    const uint16_t hit_notes[] = {988, 1319, 1568, 1319, 1760, 1568};
    uint32_t interval = phase ? REVIVE_MUSIC_HIT_STEP_MS : REVIVE_MUSIC_AIM_STEP_MS;
    uint8_t note_count = phase ? 6U : 4U;
    const uint16_t *notes = phase ? hit_notes : aim_notes;
    if (last_phase != phase) {
        last_phase = phase;
        step = 0U;
        step_tick = now;
        buzzer_tone(&buzzer_cfg, notes[step], 18);
        return;
    }
    if ((now - step_tick) >= interval) {
        step_tick = now;
        step++;
        if (step >= note_count) {
            step = 0U;
        }
        buzzer_tone(&buzzer_cfg, notes[step], 18);
    } else if ((now - step_tick) >= REVIVE_MUSIC_TONE_MS) {
        buzzer_off(&buzzer_cfg);
    }
}

static uint8_t press_one_tooth(void)
{
    uint8_t idx;
    if (g.action_done >= g.action_total) {
        return 1U;
    }
    idx = g.next_tooth;
    if (idx >= TOOTH_COUNT) {
        return 1U;
    }
    g.tooth_pressed[idx] = 1U;
    g.next_tooth++;
    g.action_done++;
    if (idx == g.danger_tooth) {
        return 2U;
    }
    beep(1700, 15);
    return (g.action_done >= g.action_total) ? 1U : 0U;
}

static void after_action(uint8_t actor)
{
    if (remaining_teeth() == 0U) {
        if (actor == 1U) {
            g.result = (g.mode == MODE_TWO_PLAYER) ? RES_P2_LOSE : RES_CPU_LOSE;
        } else if (actor == 2U) {
            g.result = RES_P1_LOSE;
        } else {
            g.result = RES_P1_LOSE;
        }
        g.state = ST_GAME_OVER;
        g.state_tick = HAL_GetTick();
        return;
    }
    if (actor == 1U) {
        if (g.mode == MODE_TWO_PLAYER) {
            g.p2_choice = 1U;
            g.joy2_sw_lock = sw_down(2U);
            g.state = ST_P2_SELECT;
        } else {
            g.state = ST_CPU_THINK;
        }
    } else {
        g.p1_choice = 1U;
        g.joy1_sw_lock = sw_down(1U);
        g.state = ST_P1_SELECT;
    }
    g.state_tick = HAL_GetTick();
}
// Logic and complete rules of the revive mini-game
static void handle_revive(void)
{
    uint32_t now = HAL_GetTick();
    revive_music_update();
    if (!g.revive_aligned) {
        update_revive_cursor();
        if (revive_target_hit()) {
            g.revive_aligned = 1U;
            g.revive_press_count = 0U;
            g.btn2_lock = btn2_down();
            g.press_tick = now;
            buzzer_tone(&buzzer_cfg, 2093, 18);
            return;
        }
        if ((now - g.state_tick) >= REVIVE_AIM_LIMIT_MS) {
            g.state = ST_GAME_OVER;
            g.state_tick = now;
            beep(300, 120);
            return;
        }
    } else {
        if (btn2_hit_sensitive()) {
            g.revive_press_count++;
            if ((g.revive_press_count % 4U) == 0U) {
                buzzer_tone(&buzzer_cfg, 1976, 18);
            }
        }
        if (g.revive_press_count >= REVIVE_REQUIRED_PRESS) {
            buzzer_off(&buzzer_cfg);
            g.result = RES_DRAW;
            g.state = ST_RELEASE;
            g.state_tick = now;
            beep(2200, 80);
            return;
        }
        if ((now - g.press_tick) >= REVIVE_PRESS_LIMIT_MS) {
            buzzer_off(&buzzer_cfg);
            g.state = ST_GAME_OVER;
            g.state_tick = now;
            beep(300, 120);
            return;
        }
    }
}
// Tooth-pressing animation
static void handle_action(uint8_t actor)
{
    uint8_t r;
    if ((HAL_GetTick() - g.press_tick) < PRESS_STEP_MS) {
        return;
    }
    r = press_one_tooth();
    g.press_tick = HAL_GetTick();
    if (r == 2U) {
        if (actor == 1U) {
            g.result = RES_P1_LOSE;
        } else if (actor == 2U) {
            g.result = RES_P2_LOSE;
        } else {
            g.result = RES_CPU_LOSE;
        }
        start_bite(actor);
    } else if (r == 1U) {
        after_action(actor);
    }
}

static void handle_select(uint8_t player)
{
    uint8_t maxc = max_choice();
    uint8_t left_now = joy_left(player);
    uint8_t right_now = joy_right(player);
    uint8_t *choice;
    uint8_t *left_lock;
    uint8_t *right_lock;
    if (player == 2U) {
        choice = &g.p2_choice;
        left_lock = &g.joy2_left_lock;
        right_lock = &g.joy2_right_lock;
    } else {
        choice = &g.p1_choice;
        left_lock = &g.joy1_left_lock;
        right_lock = &g.joy1_right_lock;
    }
    if (*choice > maxc) {
        *choice = maxc;
    }
    if (*choice < 1U) {
        *choice = 1U;
    }
    if (left_now && !(*left_lock)) {
        *left_lock = 1U;
        if (*choice > 1U) {
            (*choice)--;
            beep(1500, 10);
        }
    } else if (!left_now) {
        *left_lock = 0U;
    }
    if (right_now && !(*right_lock)) {
        *right_lock = 1U;
        if (*choice < maxc) {
            (*choice)++;
            beep(1500, 10);
        }
    } else if (!right_now) {
        *right_lock = 0U;
    }
    if (confirm_pressed(player)) {
        begin_action(*choice, player);
        g.state = (player == 1U) ? ST_P1_ACTION : ST_P2_ACTION;
        beep(1200, 30);
    }
}

MenuState Game3_Run(void)
{
    uint32_t now;
    uint32_t delay_ms;
    shark_init();
    while (1) {
        now = HAL_GetTick();
        Input_Read();
        if (btn1_pressed()) {
            PWM_SetDuty(&pwm_cfg, 50);
            buzzer_off(&buzzer_cfg);
            return MENU_STATE_HOME;
        }
        switch (g.state) {
        case ST_LOADING:
            if ((now - g.state_tick) >= LOADING_MS) {
                g.state = ST_MODE_MENU;
                g.state_tick = now;
                g.joy1_sw_lock = sw_down(1U);
                g.joy1_up_lock = 0U;
                g.joy1_down_lock = 0U;
                beep(1200, 25);
            }
            break;
        case ST_MODE_MENU:
        {
            uint8_t up_now = joy_up(1U);
            uint8_t down_now = joy_down(1U);
            if (up_now && !g.joy1_up_lock) {
                g.joy1_up_lock = 1U;
                g.menu_choice = 0U;
                beep(1500, 10);
            } else if (!up_now) {
                g.joy1_up_lock = 0U;
            }
            if (down_now && !g.joy1_down_lock) {
                g.joy1_down_lock = 1U;
                g.menu_choice = 1U;
                beep(1500, 10);
            } else if (!down_now) {
                g.joy1_down_lock = 0U;
            }
            if (confirm_pressed(1U)) {
                g.mode = (g.menu_choice == 0U) ? MODE_SINGLE : MODE_TWO_PLAYER;
                reset_round();
                g.state = ST_COUNTDOWN;
                g.state_tick = now;
                beep(1200, 40);
            }
            break;
        }
        case ST_COUNTDOWN:
            if ((now - g.state_tick) >= COUNTDOWN_MS) {
                g.state = ST_SHOW_DANGER;
                g.state_tick = now;
            }
            break;
        case ST_SHOW_DANGER:
            if ((now - g.state_tick) >= DANGER_SHOW_MS) {
                g.p1_choice = 1U;
                g.joy1_sw_lock = sw_down(1U);
                g.state = ST_P1_SELECT;
                g.state_tick = now;
            }
            break;
        case ST_P1_SELECT:
            if (max_choice() == 0U) {
                g.result = RES_P1_LOSE;
                g.state = ST_GAME_OVER;
                g.state_tick = now;
            } else {
                handle_select(1U);
            }
            break;
        case ST_P2_SELECT:
            if (max_choice() == 0U) {
                g.result = RES_P2_LOSE;
                g.state = ST_GAME_OVER;
                g.state_tick = now;
            } else {
                handle_select(2U);
            }
            break;
        case ST_P1_ACTION:
            handle_action(1U);
            break;
        case ST_P2_ACTION:
            handle_action(2U);
            break;
        case ST_BITE:
            if ((now - g.state_tick) >= BITE_ANIM_MS) {
                buzzer_off(&buzzer_cfg);
                if (g.mode == MODE_TWO_PLAYER && (g.bite_actor == 1U || g.bite_actor == 2U)) {
                    start_revive(g.bite_actor);
                } else {
                    g.state = ST_GAME_OVER;
                    g.state_tick = now;
                }
            }
            break;
        case ST_REVIVE:
            handle_revive();
            break;
        case ST_RELEASE:
            if ((now - g.state_tick) >= RELEASE_ANIM_MS) {
                g.state = ST_GAME_OVER;
                g.state_tick = now;
            }
            break;
        case ST_CPU_THINK:
            if ((now - g.state_tick) >= CPU_THINK_MS) {
                g.cpu_choice = random_1_to(max_choice());
                g.state = ST_CPU_SHOW;
                g.state_tick = now;
            }
            break;
        case ST_CPU_SHOW:
            if ((now - g.state_tick) >= CPU_SHOW_MS) {
                begin_action(g.cpu_choice, 3U);
                g.state = ST_CPU_ACTION;
            }
            break;
        case ST_CPU_ACTION:
            handle_action(3U);
            break;
        case ST_GAME_OVER:
            if ((now - g.state_tick) > GAME_OVER_HOLD_MS) {
                if (confirm_pressed(1U)) {
                    reset_round();
                    g.state = ST_COUNTDOWN;
                    g.state_tick = now;
                    beep(1200, 40);
                }
            }
            break;
        default:
            g.state = ST_MODE_MENU;
            break;
        }
        now = HAL_GetTick();
        if (g.state == ST_REVIVE) {
            if ((now - g.render_tick) >= FRAME_MS) {
                render();
                g.render_tick = now;
            }
            delay_ms = REVIVE_FRAME_MS;// The revive mini-game samples input more frequently. To avoid LCD lag caused by overly frequent refreshing, the screen is still refreshed according to FRAME_MS
        } else {
            render();
            delay_ms = FRAME_MS;
        }
        HAL_Delay(delay_ms);
    }
}
