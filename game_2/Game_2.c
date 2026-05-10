/**
 * @file Game_2.c
 * @brief Memory Match (Kitten Theme) - fully adapted
 */

#include "Game_2.h"
#include "InputHandler.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include "rng.h"
#include "Joystick.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// extern hardware
extern RNG_HandleTypeDef hrng;
extern InputState current_input;
extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;

// Direct BT2 read: PC2
#define BT2_GPIO_PORT GPIOC
#define BT2_GPIO_PIN  GPIO_PIN_2
#define BT2_ACTIVE_LEVEL GPIO_PIN_RESET

static bool bt2_is_pressed(void)
{
    if(current_input.btn2_pressed) return true;
    return (HAL_GPIO_ReadPin(BT2_GPIO_PORT, BT2_GPIO_PIN) == BT2_ACTIVE_LEVEL);
}

static void wait_btn2_btn3_released(void)
{
    do {
        Input_Read();
        HAL_Delay(10);
    } while(bt2_is_pressed() || current_input.btn3_pressed);
}

// constants
#define MAX_GRID_SIZE 4
#define EASY_ROWS 2
#define EASY_COLS 4
#define EASY_PAIRS 4
#define CHALLENGE_ROWS 4
#define CHALLENGE_COLS 4
#define CHALLENGE_PAIRS 8
#define MAX_CARDS (MAX_GRID_SIZE * MAX_GRID_SIZE)

#define LCD_W 240
#define LCD_H 240
#define CARD_WIDTH 40
#define CARD_HEIGHT 40
#define CARD_MARGIN 8

#define MISMATCH_DELAY_MS 1000
#define WIN_AUTO_RETURN_MS 3000
#define MOVE_BEEP_MS 10
#define MATCH_BEEP_FREQ 2000
#define MATCH_BEEP_DUR 80
#define MISMATCH_BEEP_FREQ 400
#define MISMATCH_BEEP_DUR 60
#define MOVE_BEEP_FREQ 1200

#define COLOR_BG       14   // light blue background
#define COLOR_CARD_BG  1    // white card
#define COLOR_PAW      7    // pink
#define COLOR_CAT_WHITE   1   // white
#define COLOR_CAT_BORDER  0   // black
#define COLOR_SHAPE    0    // black shape
#define COLOR_CURSOR   0    // black border
#define COLOR_MATCH_OK 10   // match success highlight gold
#define COLOR_TEXT     0    // black text
#define COLOR_BOW      11   // bow interior
#define COLOR_HEART    2   // red heart

// shapes
typedef enum {
    SHAPE_CATHEAD = 0,  // kitty
    SHAPE_PIG,          // piggy
    SHAPE_BEAR,         // little bear
    SHAPE_BUNNY,        // bunny
    SHAPE_HEART,        // heart
    SHAPE_CROWN,        // crown
    SHAPE_FISHBONE,     // fishbone
    SHAPE_BOW           // bow
} ShapeType;

typedef enum {
    GAME2_MODE_EASY = 0,
    GAME2_MODE_CHALLENGE
} Game2Mode;

// kitty head function
static void draw_cat_head(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER; // black
    uint16_t light  = 5;                // orange (light part)
    uint16_t dark   = 12;               // brown (dark part)
    uint16_t mouth  = COLOR_CARD_BG;    // white mouth
    // black outline
    LCD_Draw_Rect(x + 3,  y + 0,  3, 3, border, 1);
    LCD_Draw_Rect(x + 21, y + 0,  3, 3, border, 1);
    LCD_Draw_Rect(x + 0,  y + 3,  3, 18, border, 1);
    LCD_Draw_Rect(x + 24, y + 3,  3, 18, border, 1);
    LCD_Draw_Rect(x + 6,  y + 3,  3, 3, border, 1);
    LCD_Draw_Rect(x + 18, y + 3,  3, 3, border, 1);
    LCD_Draw_Rect(x + 3,  y + 21, 21, 3, border, 1);
    // orange body
    LCD_Draw_Rect(x + 3,  y + 3,  3, 3, light, 1);
    LCD_Draw_Rect(x + 21,  y + 3,  3, 3, light, 1);
    LCD_Draw_Rect(x + 3, y + 6,  21, 15, light, 1);
    // dark brown pattern
    LCD_Draw_Rect(x + 3,  y + 12, 3, 3, dark, 1);
    LCD_Draw_Rect(x + 21, y + 12, 3, 3, dark, 1);
    LCD_Draw_Rect(x + 3,  y + 18, 3, 3, dark, 1);
    LCD_Draw_Rect(x + 21, y + 18, 3, 3, dark, 1);

    LCD_Draw_Rect(x + 9, y + 6,  9, 3, border, 1);
    LCD_Draw_Rect(x + 9, y + 12, 3, 6, border, 1);
    LCD_Draw_Rect(x + 15, y + 12, 3, 6, border, 1);
    // white mouth
    LCD_Draw_Rect(x + 12, y + 15, 3, 3, mouth, 1);
    LCD_Draw_Rect(x + 9, y + 18, 9, 3, mouth, 1);
}

// bow function
static void draw_bow(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t bow    = COLOR_BOW;
    // black outline
    LCD_Draw_Rect(x + 10,  y + 11,  6, 2, border, 1);
    LCD_Draw_Rect(x + 24,  y + 11,  6, 2,  border, 1);
    LCD_Draw_Rect(x + 8, y + 13,  10, 2,  border, 1);
    LCD_Draw_Rect(x + 22, y + 13,  10, 2,  border, 1);
    LCD_Draw_Rect(x + 6,  y + 15,  28, 10,  border, 1);
    LCD_Draw_Rect(x + 8, y + 25,  10, 2,  border, 1);
    LCD_Draw_Rect(x + 22, y + 25, 10, 2,  border, 1);
    LCD_Draw_Rect(x + 10,  y + 27, 6, 2,  border, 1);
    LCD_Draw_Rect(x + 24,  y + 27, 6, 2,  border, 1);
    // red interior
    LCD_Draw_Rect(x + 8,  y + 15,  2, 10, bow, 1);
    LCD_Draw_Rect(x + 10,  y + 13,  6, 14, bow, 1);
    LCD_Draw_Rect(x + 12,  y + 15, 2, 2, bow, 1);
    LCD_Draw_Rect(x + 16,  y + 23, 2, 2, bow, 1);
    LCD_Draw_Rect(x + 18, y + 17, 4, 6, bow, 1);
    LCD_Draw_Rect(x + 22, y + 15,  2, 2, bow, 1);
    LCD_Draw_Rect(x + 22, y + 23,  2, 2, bow, 1);
    LCD_Draw_Rect(x + 24, y + 13, 6, 14, bow, 1);
    LCD_Draw_Rect(x + 30, y + 15, 2, 10, bow, 1);
}

// heart function
static void draw_heart(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t heart  = COLOR_HEART;
    // outline
    LCD_Draw_Rect(x + 9,  y + 0,  6,  3, border, 1);
    LCD_Draw_Rect(x + 21, y + 0,  6,  3, border, 1);
    LCD_Draw_Rect(x + 6,  y + 3,  3,  3, border, 1);
    LCD_Draw_Rect(x + 15, y + 3,  6,  3, border, 1);
    LCD_Draw_Rect(x + 27, y + 3,  3,  3, border, 1);
    LCD_Draw_Rect(x + 3,  y + 6,  3,  9, border, 1);
    LCD_Draw_Rect(x + 30, y + 6,  3,  9, border, 1);
    LCD_Draw_Rect(x + 6,  y + 15, 3,  3, border, 1);
    LCD_Draw_Rect(x + 27, y + 15, 3,  3, border, 1);
    LCD_Draw_Rect(x + 9,  y + 18, 3,  3, border, 1);
    LCD_Draw_Rect(x + 24, y + 18, 3,  3, border, 1);
    LCD_Draw_Rect(x + 12, y + 21, 3,  3, border, 1);
    LCD_Draw_Rect(x + 21, y + 21, 3,  3, border, 1);
    LCD_Draw_Rect(x + 15, y + 24, 6,  3, border, 1);
    // interior
    LCD_Draw_Rect(x + 9,  y + 3,  6,  3, heart, 1);
    LCD_Draw_Rect(x + 21, y + 3,  6,  3, heart, 1);
    LCD_Draw_Rect(x + 6,  y + 6,  12, 3, heart, 1);
    LCD_Draw_Rect(x + 21, y + 6,  9,  3, heart, 1);
    LCD_Draw_Rect(x + 6,  y + 9,  24, 3, heart, 1);
    LCD_Draw_Rect(x + 6,  y + 12, 24, 3, heart, 1);
    LCD_Draw_Rect(x + 9,  y + 15, 18, 3, heart, 1);
    LCD_Draw_Rect(x + 12, y + 18, 12, 3, heart, 1);
    LCD_Draw_Rect(x + 15, y + 21, 6,  3, heart, 1);
}

// fishbone function
static void draw_fishbone(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t bone   = 13; 
    // black outline
    LCD_Draw_Rect(x + 8,  y + 0,  4, 2, border, 1);
    LCD_Draw_Rect(x + 4,  y + 2,  10, 14, border, 1);
    LCD_Draw_Rect(x + 8,  y + 16, 4, 2, border, 1);
    LCD_Draw_Rect(x + 0,  y + 6,  2, 6, border, 1);
    LCD_Draw_Rect(x + 2,  y + 4, 2, 10, border, 1);
    LCD_Draw_Rect(x + 14, y + 8,  10, 2, border, 1);
    LCD_Draw_Rect(x + 16, y + 4, 2, 10, border, 1);
    LCD_Draw_Rect(x + 20, y + 6, 2, 6, border, 1);
    LCD_Draw_Rect(x + 24, y + 6,  2, 6, border, 1);
    LCD_Draw_Rect(x + 26, y + 4, 2, 10, border, 1);
    LCD_Draw_Rect(x + 28, y + 2,  2, 14, border, 1);
    // light blue interior
    LCD_Draw_Rect(x + 2,  y + 6,  2, 6, bone, 1);
    LCD_Draw_Rect(x + 8,  y + 2,  4, 2, bone, 1);
    LCD_Draw_Rect(x + 4,  y + 4,  8, 10, bone, 1);
    LCD_Draw_Rect(x + 8,  y + 14, 4, 2, bone, 1);
    LCD_Draw_Rect(x + 24, y + 8,  2, 2, bone, 1);
    LCD_Draw_Rect(x + 26, y + 6,  2, 6, bone, 1);
    // eye
    LCD_Draw_Rect(x + 6, y + 8,  2, 2, border, 1);
}

// crown function
static void draw_crown(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t crown   = 10; 
    // black border
    LCD_Draw_Rect(x + 15, y + 0,  3, 3, border, 1);
    LCD_Draw_Rect(x + 12, y + 3,  9, 3, border, 1);
    LCD_Draw_Rect(x + 3, y + 6,  3, 3, border, 1);
    LCD_Draw_Rect(x + 15, y + 6,  3, 3, border, 1);
    LCD_Draw_Rect(x + 27, y + 6,  3, 3, border, 1);
    LCD_Draw_Rect(x + 0, y + 9,  9, 3, border, 1);
    LCD_Draw_Rect(x + 12, y + 9,  9, 3, border, 1);
    LCD_Draw_Rect(x + 24, y + 9,  9, 3, border, 1);
    LCD_Draw_Rect(x + 3, y + 12,  27, 6, border, 1);
    LCD_Draw_Rect(x + 6, y + 18,  21, 9, border, 1);
    
    // golden interior
    LCD_Draw_Rect(x + 15, y + 3,  3, 3, crown, 1);
    LCD_Draw_Rect(x + 3, y + 9,  3, 3, crown, 1);
    LCD_Draw_Rect(x + 15, y + 9,  3, 3, crown, 1);
    LCD_Draw_Rect(x + 27, y + 9,  3, 3, crown, 1);
    LCD_Draw_Rect(x + 6, y + 12,  21, 6, crown, 1);
    LCD_Draw_Rect(x + 9, y + 18,  15, 6, crown, 1);
    LCD_Draw_Rect(x + 9, y + 12,  3, 3, border, 1);
    LCD_Draw_Rect(x + 21, y + 12,  3, 3, border, 1);
}

// bunny function
static void draw_bunny(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t blush  = COLOR_PAW;
    // black outline
    LCD_Draw_Rect(x + 9, y + 0,  3, 3, border, 1);
    LCD_Draw_Rect(x + 15, y + 0,  3, 3, border, 1);
    LCD_Draw_Rect(x + 6,  y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 12, y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 18, y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 3, y + 9,  3, 3, border, 1);
    LCD_Draw_Rect(x + 21, y + 9,  3, 3, border, 1);
    LCD_Draw_Rect(x + 0,  y + 12, 3, 12, border, 1);
    LCD_Draw_Rect(x + 24, y + 12, 3, 12, border, 1);
    LCD_Draw_Rect(x + 9, y + 15, 3, 6, border, 1);
    LCD_Draw_Rect(x + 15, y + 15, 3, 6, border, 1);
    LCD_Draw_Rect(x + 3, y + 24, 21, 3, border, 1);
    // pink blush
    LCD_Draw_Rect(x + 3,  y + 18, 6, 6, blush, 1);
    LCD_Draw_Rect(x + 18, y + 18, 6, 6, blush, 1);
}

// bear function
static void draw_bear(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;  // brown
    uint16_t brown = 12;  // light skin tone
    uint16_t face = 1;
    // black outline
    LCD_Draw_Rect(x + 3,  y + 0,  6, 3, border, 1);
    LCD_Draw_Rect(x + 24, y + 0,  6, 3, border, 1);
    LCD_Draw_Rect(x + 9,  y + 3,  15, 3, border, 1);
    LCD_Draw_Rect(x + 0,  y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 30, y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 3,  y + 9,  3, 12, border, 1);
    LCD_Draw_Rect(x + 27, y + 9,  3, 12, border, 1);
    LCD_Draw_Rect(x + 6, y + 6,  3, 3, border, 1);
    LCD_Draw_Rect(x + 24, y + 6,  3, 3, border, 1);
    LCD_Draw_Rect(x + 6,  y + 21, 21, 3, border, 1);
    LCD_Draw_Rect(x + 12, y + 9,  3, 6, border, 1);
    LCD_Draw_Rect(x + 18, y + 9,  3, 6, border, 1);
    // brown parts
    LCD_Draw_Rect(x + 3,  y + 3,  6, 3, brown, 1);
    LCD_Draw_Rect(x + 24, y + 3,  6, 3, brown, 1);
    LCD_Draw_Rect(x + 3,  y + 6,  3, 3, brown, 1);
    LCD_Draw_Rect(x + 27, y + 6,  3, 3, brown, 1);
    // face
    LCD_Draw_Rect(x + 9,  y + 6,  15, 3, brown, 1);
    LCD_Draw_Rect(x + 6,  y + 9,  6, 12, brown, 1);
    LCD_Draw_Rect(x + 21, y + 9,  6, 12, brown, 1);
    LCD_Draw_Rect(x + 15, y + 9,  3, 3, brown, 1);
    // light mouth area
    LCD_Draw_Rect(x + 15, y + 12, 3, 3, face, 1);
    LCD_Draw_Rect(x + 12, y + 15, 9, 6, face, 1);
    // nose
    LCD_Draw_Rect(x + 15, y + 15, 3, 3, border, 1);
}

// piggy function
static void draw_pig(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER; 
    uint16_t pink   = 7; 
    uint16_t nose   = 15;
    uint16_t nostril = 2;
    // black outline
    LCD_Draw_Rect(x + 3,  y + 0,  27, 3, border, 1);
    LCD_Draw_Rect(x + 0,  y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 30, y + 3,  3, 6, border, 1);
    LCD_Draw_Rect(x + 3,  y + 6,  3, 12, border, 1);
    LCD_Draw_Rect(x + 27, y + 6,  3, 12, border, 1);
    LCD_Draw_Rect(x + 6,  y + 18, 21, 3, border, 1);
    // light pink face
    LCD_Draw_Rect(x + 3,  y + 3,  27, 3, pink, 1);
    LCD_Draw_Rect(x + 6,  y + 6,  21, 12, pink, 1);
    // eyes
    LCD_Draw_Rect(x + 12, y + 6,  3, 6, border, 1);
    LCD_Draw_Rect(x + 18, y + 6,  3, 6, border, 1);
    // nose
    LCD_Draw_Rect(x + 9,  y + 12, 15, 6, nose, 1);
    // nostrils
    LCD_Draw_Rect(x + 12, y + 15, 3, 3, nostril, 1);
    LCD_Draw_Rect(x + 18, y + 15, 3, 3, nostril, 1);
}

// kitty paw
static void draw_kitten_paw(uint16_t x, uint16_t y)
{
    uint16_t border = COLOR_CAT_BORDER;
    uint16_t paw  = COLOR_PAW;
    // black outline
    LCD_Draw_Rect(x + 9,  y + 5,  21, 3, border, 1);
    LCD_Draw_Rect(x + 6,  y + 8,  27, 3, border, 1);
    LCD_Draw_Rect(x + 3,  y + 11,   33, 15, border, 1);
    LCD_Draw_Rect(x + 6,  y + 26,  27, 6, border, 1);
    LCD_Draw_Rect(x + 9,  y + 32,  21, 3, border, 1);
    // pink paw
    LCD_Draw_Rect(x + 12, y + 8,  6, 6, paw, 1);
    LCD_Draw_Rect(x + 21, y + 8,  6, 6, paw, 1);
    LCD_Draw_Rect(x + 6,  y + 14, 6, 6, paw, 1);
    LCD_Draw_Rect(x + 27, y + 14, 6, 6, paw, 1);
    LCD_Draw_Rect(x + 15, y + 17, 9, 3, paw, 1);
    LCD_Draw_Rect(x + 12, y + 20, 15, 3, paw, 1);
    LCD_Draw_Rect(x + 9,  y + 23, 21, 6, paw, 1);
    LCD_Draw_Rect(x + 12, y + 29, 15, 3, paw, 1);
}


// game state 
static uint8_t card_shape[MAX_GRID_SIZE][MAX_GRID_SIZE];
static bool card_matched[MAX_GRID_SIZE][MAX_GRID_SIZE];
static bool card_face_up[MAX_GRID_SIZE][MAX_GRID_SIZE];
static uint8_t matched_count;
static uint8_t active_rows = CHALLENGE_ROWS;
static uint8_t active_cols = CHALLENGE_COLS;
static uint8_t active_pairs = CHALLENGE_PAIRS;
static Game2Mode active_mode = GAME2_MODE_CHALLENGE;
typedef enum {STATE_IDLE, STATE_FIRST_SELECTED, STATE_MISMATCH_DELAY} GameState;
static GameState game_state = STATE_IDLE;
static int8_t first_x = -1, first_y = -1;
static uint8_t first_shape;
static uint32_t mismatch_end_time = 0;
static int8_t cursor_x = 0, cursor_y = 0;
static bool win_active = false;
static uint32_t win_return_time = 0;
static bool game2_request_home = false;

static uint16_t grid_start_x(void)
{
    return (LCD_W - (active_cols * CARD_WIDTH + (active_cols - 1) * CARD_MARGIN)) / 2;
}

static uint16_t grid_start_y(void)
{
    return (active_rows == EASY_ROWS) ? 82 : 43;
}

static void draw_card(int row,int col,bool highlight){
    uint16_t x=grid_start_x()+col*(CARD_WIDTH+CARD_MARGIN);
    uint16_t y=grid_start_y()+row*(CARD_HEIGHT+CARD_MARGIN);
    // card shadow
    LCD_Draw_Rect(x + 40, y + 3, 3, 40, 8, 1);
    LCD_Draw_Rect(x + 3, y + 40, 40, 3, 8, 1);
    // card body
    LCD_Draw_Rect(x, y, CARD_WIDTH, CARD_HEIGHT, COLOR_CARD_BG, 1);
    // card highlight edge
    LCD_Draw_Rect(x, y, CARD_WIDTH, 2, 15, 1);
    LCD_Draw_Rect(x, y, 2, CARD_HEIGHT, 15, 1);
    // card black border
    LCD_Draw_Rect(x, y, CARD_WIDTH, CARD_HEIGHT, COLOR_CURSOR, 0);


    if(card_matched[row][col] || card_face_up[row][col])
    {
        if(card_shape[row][col] == SHAPE_CATHEAD)
        {
            draw_cat_head(x + 6, y + 8);
        }
        else if(card_shape[row][col] == SHAPE_PIG)
        {
            draw_pig(x + 3, y + 9);
        }
        else if(card_shape[row][col] == SHAPE_BEAR)
        {
            draw_bear(x + 4, y + 8);
        }
        else if(card_shape[row][col] == SHAPE_BUNNY)
        {
            draw_bunny(x + 6, y + 6);
        }
        else if(card_shape[row][col] == SHAPE_HEART)
        {
            draw_heart(x + 3, y + 6);
        }
        else if(card_shape[row][col] == SHAPE_CROWN)
        {
            draw_crown(x + 3, y + 6);
        }
        else if(card_shape[row][col] == SHAPE_FISHBONE)
        {
            draw_fishbone(x + 5, y + 11);
        }
        else
        {
            draw_bow(x, y);
        }
    }
    else
    {
        draw_kitten_paw(x+1, y);
    }
    // joystick selection box
    if(highlight)
    {
        LCD_Draw_Rect(x - 4, y - 4,
                    CARD_WIDTH + 8,
                    CARD_HEIGHT + 8,
                    10, 0);
        
        LCD_Draw_Rect(x - 3, y - 3,
                    CARD_WIDTH + 6,
                    CARD_HEIGHT + 6,
                    10, 0);

        LCD_Draw_Rect(x - 2, y - 2,
                    CARD_WIDTH + 4,
                    CARD_HEIGHT + 4,
                    10, 0);

        LCD_Draw_Rect(x - 1, y - 1,
                    CARD_WIDTH + 2,
                    CARD_HEIGHT + 2,
                    10, 0);
    }
}

static void redraw_game_screen(void){
    LCD_Fill_Buffer(COLOR_BG);
    for(int row=0;row<active_rows;row++)
        for(int col=0;col<active_cols;col++)
            draw_card(row,col,row==cursor_y && col==cursor_x);
    char status[32];
    sprintf(status,"Matched: %d/%d",matched_count,active_pairs);
    LCD_printString(status,15,15,COLOR_TEXT,2);
    LCD_Refresh(&cfg0);
}

static void flash_match(int r1,int c1,int r2,int c2){
    LCD_Draw_Rect(grid_start_x()+c1*(CARD_WIDTH+CARD_MARGIN),
                  grid_start_y()+r1*(CARD_HEIGHT+CARD_MARGIN),
                  CARD_WIDTH,CARD_HEIGHT,COLOR_MATCH_OK,1);
    LCD_Draw_Rect(grid_start_x()+c2*(CARD_WIDTH+CARD_MARGIN),
                  grid_start_y()+r2*(CARD_HEIGHT+CARD_MARGIN),
                  CARD_WIDTH,CARD_HEIGHT,COLOR_MATCH_OK,1);
    LCD_Refresh(&cfg0);
    HAL_Delay(150);
    draw_card(r1,c1,cursor_y==r1 && cursor_x==c1);
    draw_card(r2,c2,cursor_y==r2 && cursor_x==c2);
    LCD_Refresh(&cfg0);
}


static bool show_loading_screen(void)
{
    const int bar_x = 45;
    const int bar_y = 22;
    const int bar_w = 140;
    const int bar_h = 20;

    const int inner_x = bar_x + 4;
    const int inner_y = bar_y + 4;
    const int inner_w = bar_w - 8;
    const int inner_h = bar_h - 8;

    const int steps = 20;
    const int delay_ms = 70;


    for(int step = 0; step <= steps; step++)
    {
        Input_Read();
        if(bt2_is_pressed()) return false;
        LCD_Fill_Buffer(COLOR_BG);
        // loading progress bar outer frame
        LCD_Draw_Rect(bar_x, bar_y, bar_w, bar_h, COLOR_CURSOR, 0);
        LCD_Draw_Rect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, COLOR_CURSOR, 0);
        // progress bar interior
        LCD_Draw_Rect(inner_x, inner_y, inner_w, inner_h, COLOR_BG, 1);

        int fill_w = (inner_w * step) / steps;
        if(fill_w > 0)
        {
            LCD_Draw_Rect(inner_x, inner_y, fill_w, inner_h, COLOR_CURSOR, 1);
        }
        // center title text
        LCD_printString("Get ready", 40, 65, COLOR_TEXT, 3);
        LCD_printString("Get ready", 41, 65, COLOR_TEXT, 3);
        LCD_printString("to match!", 40, 100, COLOR_TEXT, 3);
        LCD_printString("to match!", 41, 100, COLOR_TEXT, 3);
        // how to play
        LCD_printString("How to play:", 25, 145, COLOR_TEXT, 2);
        LCD_printString("Move joystick to select", 25, 170, COLOR_TEXT, 1);
        LCD_printString("Press BT3 to flip", 25, 185, COLOR_TEXT, 1);
        LCD_printString("Match all pairs!", 25, 200, COLOR_TEXT, 1);
        LCD_printString("Press BT2 to pause",25,215,COLOR_TEXT,1);
        // kitten paw illustration
        draw_kitten_paw(160, 12);

        LCD_Refresh(&cfg0);
        for(int t = 0; t < delay_ms / 10; t++){
            HAL_Delay(10);
            Input_Read();
            if(bt2_is_pressed()) return false;
        }
    }
    return true;
}


static void show_win_screen(void){
    LCD_Fill_Buffer(COLOR_BG);
    LCD_printString("Meow! You win!",20,60,COLOR_TEXT,2);
    LCD_printString("Meow! You win!",19,60,COLOR_TEXT,2);
    char win_status[32];
    sprintf(win_status,"Pairs matched: %d/%d",matched_count,active_pairs);
    LCD_printString(win_status,20,100,COLOR_TEXT,2);
    LCD_printString(win_status,19,100,COLOR_TEXT,2);

    for(int i=0; i<3; i++){
        draw_kitten_paw(110,140);
        LCD_Refresh(&cfg0);
        for(int t=0; t<6; t++){
            HAL_Delay(50);
            Input_Read();
            if(bt2_is_pressed()){
                game2_request_home = true;
                win_return_time = HAL_GetTick();
                return;
            }
        }

        LCD_Draw_Rect(110, 140, 40, 25, COLOR_BG, 1);
        LCD_Refresh(&cfg0);
        for(int t=0; t<4; t++){
            HAL_Delay(50);
            Input_Read();
            if(bt2_is_pressed()){
                game2_request_home = true;
                win_return_time = HAL_GetTick();
                return;
            }
        }
    }

    draw_kitten_paw(90,140);
    LCD_printString("Returning to Game2 menu...",25,200,COLOR_TEXT,1);
    LCD_Refresh(&cfg0);

    win_return_time = HAL_GetTick() + WIN_AUTO_RETURN_MS;
}

// game logic
static void init_game(Game2Mode mode){
    srand(HAL_GetTick());
    active_mode = mode;
    if(mode == GAME2_MODE_EASY){
        active_rows = EASY_ROWS;
        active_cols = EASY_COLS;
        active_pairs = EASY_PAIRS;
    } else {
        active_rows = CHALLENGE_ROWS;
        active_cols = CHALLENGE_COLS;
        active_pairs = CHALLENGE_PAIRS;
    }

    uint8_t total_cards = active_rows * active_cols;
    uint8_t pool[MAX_CARDS];
    for(int i=0;i<active_pairs;i++){pool[2*i]=i; pool[2*i+1]=i;}
    for(int i=total_cards-1;i>0;i--){
        uint32_t r; if(HAL_RNG_GenerateRandomNumber(&hrng,&r)!=HAL_OK) r=rand();
        int j=r%(i+1); uint8_t tmp=pool[i]; pool[i]=pool[j]; pool[j]=tmp;
    }
    for(int row=0;row<MAX_GRID_SIZE;row++)
        for(int col=0;col<MAX_GRID_SIZE;col++){
            card_shape[row][col]=0;
            card_matched[row][col]=false;
            card_face_up[row][col]=false;
        }

    for(int row=0;row<active_rows;row++)
        for(int col=0;col<active_cols;col++){
            card_shape[row][col]=pool[row*active_cols+col];
            card_matched[row][col]=false;
            card_face_up[row][col]=false;
        }
    matched_count=0; game_state=STATE_IDLE; first_x=first_y=-1;
    cursor_x=0; cursor_y=0; win_active=false;
    redraw_game_screen();
}

static void check_win(void){
    if(matched_count==active_pairs && !win_active){
        win_active=true;
        show_win_screen();
    }
}


static void select_card(int row,int col){
    if(card_matched[row][col]||card_face_up[row][col]) return;
    switch(game_state){
        case STATE_IDLE:
            card_face_up[row][col]=true;
            first_x=row; first_y=col; first_shape=card_shape[row][col];
            game_state=STATE_FIRST_SELECTED;
            redraw_game_screen();
            break;
        case STATE_FIRST_SELECTED:
            if(row==first_x && col==first_y) return;
            card_face_up[row][col]=true; redraw_game_screen();
            if(card_shape[row][col]==first_shape){
                card_matched[first_x][first_y]=true; card_matched[row][col]=true;
                matched_count++;
                buzzer_tone(&buzzer_cfg,MATCH_BEEP_FREQ,MATCH_BEEP_DUR); HAL_Delay(MATCH_BEEP_DUR); buzzer_off(&buzzer_cfg);
                flash_match(first_x,first_y,row,col); game_state=STATE_IDLE; first_x=first_y=-1;
                redraw_game_screen(); check_win();
            } else {
                game_state=STATE_MISMATCH_DELAY; mismatch_end_time=HAL_GetTick()+MISMATCH_DELAY_MS;
                buzzer_tone(&buzzer_cfg,MISMATCH_BEEP_FREQ,MISMATCH_BEEP_DUR); HAL_Delay(MISMATCH_BEEP_DUR); buzzer_off(&buzzer_cfg);
            }
            break;
        default: break;
    }
}

static void handle_mismatch_delay(void){
    if(game_state==STATE_MISMATCH_DELAY && HAL_GetTick()>=mismatch_end_time){
        for(int r=0;r<active_rows;r++) for(int c=0;c<active_cols;c++)
            if(card_face_up[r][c]&&!card_matched[r][c]) card_face_up[r][c]=false;
        game_state=STATE_IDLE; first_x=first_y=-1; redraw_game_screen();
    }
}

static void handle_input(void){
    static uint32_t last_move=0; uint32_t now=HAL_GetTick();
    if(now-last_move<150) return;
    Joystick_t jd; Joystick_Read(&joystick_cfg,&jd);
    int dx=0,dy=0;
    if(jd.x_processed<-joystick_cfg.deadzone) dx=-1;
    else if(jd.x_processed>joystick_cfg.deadzone) dx=1;
    if(jd.y_processed<-joystick_cfg.deadzone) dy=-1;
    else if(jd.y_processed>joystick_cfg.deadzone) dy=1;
    if(dx!=0||dy!=0){
        int nx=cursor_x+dx, ny=cursor_y+dy;
        if(nx>=0 && nx<active_cols) cursor_x=nx;
        if(ny>=0 && ny<active_rows) cursor_y=ny;
        last_move=now;
        redraw_game_screen();
    }
    if(current_input.btn3_pressed && game_state!=STATE_MISMATCH_DELAY && !win_active){
        select_card(cursor_y,cursor_x); current_input.btn3_pressed=false;
    }
}

static void draw_filled_round_rect(uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h,
                                   uint16_t r, uint16_t color)
{
    // Center body
    LCD_Draw_Rect(x + r, y, w - 2 * r, h, color, 1);
    LCD_Draw_Rect(x, y + r, w, h - 2 * r, color, 1);

    // Pixel-style rounded corners
    LCD_Draw_Rect(x + 2, y + 1, r - 2, 1, color, 1);
    LCD_Draw_Rect(x + 1, y + 2, r - 1, r - 2, color, 1);

    LCD_Draw_Rect(x + w - r, y + 1, r - 2, 1, color, 1);
    LCD_Draw_Rect(x + w - r, y + 2, r - 1, r - 2, color, 1);

    LCD_Draw_Rect(x + 1, y + h - r, r - 1, r - 2, color, 1);
    LCD_Draw_Rect(x + 2, y + h - 2, r - 2, 1, color, 1);

    LCD_Draw_Rect(x + w - r, y + h - r, r - 1, r - 2, color, 1);
    LCD_Draw_Rect(x + w - r, y + h - 2, r - 2, 1, color, 1);
}

static void draw_game2_submenu(uint8_t selected)
{
    LCD_Fill_Buffer(COLOR_BG);
    LCD_printString("Game 2", 72, 35, COLOR_TEXT, 3);
    LCD_printString("Kitty Match", 48, 70, COLOR_TEXT, 2);

    const char *items[3] = {"Easy", "Challenge", "Back"};

    for(uint8_t i = 0; i < 3; i++){
        uint16_t y = 112 + i * 35;

        if(i == selected){
            // Filled pink rounded selection background
            draw_filled_round_rect(34, y - 8, 172, 30, 6, 10);
        }

        LCD_printString((char*)items[i], 72, y, COLOR_TEXT, 2);
    }

    LCD_printString("BT3: select", 50, 218, COLOR_TEXT, 1);
    LCD_Refresh(&cfg0);
}

static int8_t game2_submenu(void)
{
    uint8_t selected = 0;
    uint32_t last_move = 0;
    draw_game2_submenu(selected);

    wait_btn2_btn3_released();

    while(1){
        Input_Read();
        Joystick_t jd;
        Joystick_Read(&joystick_cfg, &jd);
        uint32_t now = HAL_GetTick();

        if(now - last_move > 180){
            if(jd.y_processed < -joystick_cfg.deadzone){
                if(selected > 0) selected--;
                last_move = now;
                draw_game2_submenu(selected);
            } else if(jd.y_processed > joystick_cfg.deadzone){
                if(selected < 2) selected++;
                last_move = now;
                draw_game2_submenu(selected);
            }
        }

        if(current_input.btn3_pressed){
            current_input.btn3_pressed = false;
            return (int8_t)selected;  // 0 Easy, 1 Challenge, 2 Back
        }
        if(bt2_is_pressed()){
            current_input.btn2_pressed = false;
            return 2;
        }
        HAL_Delay(30);
    }
}

// main loop
MenuState Game2_Run(void){
    game2_request_home = false;
    if(!show_loading_screen()){
        buzzer_off(&buzzer_cfg);
        return MENU_STATE_HOME;
    }

    while(1){
        if(game2_request_home){
            buzzer_off(&buzzer_cfg);
            return MENU_STATE_HOME;
        }
        int8_t choice = game2_submenu();
        if(choice == 2 || game2_request_home){
            buzzer_off(&buzzer_cfg);
            return MENU_STATE_HOME;
        }

        init_game(choice == 0 ? GAME2_MODE_EASY : GAME2_MODE_CHALLENGE);
        wait_btn2_btn3_released();

        while(1){
            Input_Read();
            if(bt2_is_pressed()){
                buzzer_off(&buzzer_cfg);
                return MENU_STATE_HOME;
            }

            if(game2_request_home){
                buzzer_off(&buzzer_cfg);
                return MENU_STATE_HOME;
            }

            if(win_active && HAL_GetTick() >= win_return_time){
                break;
            }

            handle_mismatch_delay();
            handle_input();
            HAL_Delay(30);
        }
    }
}
