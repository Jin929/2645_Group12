#include "Menu.h"
#include "LCD.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include "PWM.h"  
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;  // LCD configuration from main.c
extern Joystick_cfg_t joystick_cfg;  // Joystick configuration
extern Joystick_t joystick_data;     // Current joystick readings
extern PWM_cfg_t pwm_cfg;

// RGB LED pins 
#define RGB_RED_PORT    GPIOB
#define RGB_RED_PIN     GPIO_PIN_3
#define RGB_GREEN_PORT  GPIOB
#define RGB_GREEN_PIN   GPIO_PIN_4   
#define RGB_BLUE_PORT   GPIOB
#define RGB_BLUE_PIN    GPIO_PIN_5   

#define BLACK   0
#define WHITE   1
#define GREY    13
#define BROWN   12

#define SCREEN_W    240
#define SCREEN_H    240

static const uint8_t menu_bird[16][16] = {
    {255,255,255,255,0, 6, 6, 6, 6,0,255,255,255,255,255,255},
    {255,255,255,0, 6, 6, 6, 6, 6, 6,0, 1, 1,0,255,255},
    {255,255,0, 1, 1, 1, 6, 6, 6,0, 1, 1, 1,0,255,255},
    {255,0, 1, 1, 1, 1,0, 6,6,0, 1, 1, 0, 1,0,255},
    {0, 1, 1, 1, 1, 1,0, 6,6,0, 1, 1, 0, 1,0,255},
    {0, 1, 1, 1, 1, 1,0, 6,6,0, 1, 1, 1, 1,0,255},
    {0, 1, 1, 1, 1,0,0, 6, 6,0,0,0,0,0,0,255},
    {0,0, 1, 1,0,6,6, 6, 6, 6,0, 2, 2, 2,0,255},
    {255,0,0,0,0,6,6, 6, 6, 6,0, 2, 2, 2,0,255},
    {255,255,255,0, 5, 5, 5, 6, 6,6,0,0,255,255,255,255},
    {255,255,255,0,0, 5, 5, 5,6,6,6,6,0,255,255,255},
    {255,255,255,255,0,0, 5, 5, 5,6,6,0,255,255,255,255},
    {255,255,255,255,255,255,0, 5,0,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,0,255,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}
};

static void draw_cat_paw(int x, int y) {
    int border = 1;      
    int paw = 7; 
    
    LCD_Draw_Rect(x + 9,  y + 5,  21, 3, border, 1);
    LCD_Draw_Rect(x + 6,  y + 8,  27, 3, border, 1);
    LCD_Draw_Rect(x + 3,  y + 11, 33, 15, border, 1);
    LCD_Draw_Rect(x + 6,  y + 26, 27, 6, border, 1);
    LCD_Draw_Rect(x + 9,  y + 32, 21, 3, border, 1);

    LCD_Draw_Rect(x + 12, y + 8,  6, 6, paw, 1);
    LCD_Draw_Rect(x + 21, y + 8,  6, 6, paw, 1);
    LCD_Draw_Rect(x + 6,  y + 14, 6, 6, paw, 1);
    LCD_Draw_Rect(x + 27, y + 14, 6, 6, paw, 1);
    LCD_Draw_Rect(x + 15, y + 17, 9, 3, paw, 1);
    LCD_Draw_Rect(x + 12, y + 20, 15, 3, paw, 1);
    LCD_Draw_Rect(x + 9,  y + 23, 21, 6, paw, 1);
    LCD_Draw_Rect(x + 12, y + 29, 15, 3, paw, 1);
}

extern void draw_loading_small_shark(int cx, int cy);

static void RGB_SetColor(uint8_t red, uint8_t green, uint8_t blue) {
    //red
    if (red == 1) {
        HAL_GPIO_WritePin(RGB_RED_PORT, RGB_RED_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(RGB_RED_PORT, RGB_RED_PIN, GPIO_PIN_RESET);
    }
    
    //green
    if (green == 1) {
        HAL_GPIO_WritePin(RGB_GREEN_PORT, RGB_GREEN_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(RGB_GREEN_PORT, RGB_GREEN_PIN, GPIO_PIN_RESET);
    }
    
    //blue
    if (blue == 1) {
        HAL_GPIO_WritePin(RGB_BLUE_PORT, RGB_BLUE_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(RGB_BLUE_PORT, RGB_BLUE_PIN, GPIO_PIN_RESET);
    }
}

static void RGB_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    

    GPIO_InitStruct.Pin = RGB_RED_PIN;
    HAL_GPIO_Init(RGB_RED_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = RGB_GREEN_PIN;
    HAL_GPIO_Init(RGB_GREEN_PORT, &GPIO_InitStruct);
    
   
    GPIO_InitStruct.Pin = RGB_BLUE_PIN;
    HAL_GPIO_Init(RGB_BLUE_PORT, &GPIO_InitStruct);
    
  
    RGB_SetColor(0, 0, 0);
}

static void RGB_SetGameColor(int selected_option) {
    if (selected_option == 0) {
        RGB_SetColor(0, 0, 1);
    } else if (selected_option == 1) {
        RGB_SetColor(1, 0, 0);
    } else if (selected_option == 2) {
        RGB_SetColor(1, 0, 1);
    } else {
        RGB_SetColor(0, 0, 0);
    }
}

// Menu options
static const char* menu_options[] = {
    "Flappy Bird",
    "Kitty Match", 
    "Shark Bite"
};
#define NUM_MENU_OPTIONS 3

// Frame rate for menu (in milliseconds)
#define MENU_FRAME_TIME_MS 30  // ~33 FPS

/**
 * @brief Render the home menu screen
 */
static void render_home_menu(MenuSystem* menu) {
    LCD_Fill_Buffer(7);
    //LCD_Fill(&cfg0, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 9);
    // Title
    LCD_printString("MAIN MENU", 50, 20, 0, 3);
    LCD_printString("MAIN MENU", 51, 20, 0, 3);
    
    // Menu options with selection highlight
    for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
        uint16_t y_pos = 70 + (i * 40);
        uint8_t text_size = 2;
        
        if (i == menu->selected_option) {
            // Highlight selected option with inverted colors
            // Draw a rectangle around selected option
            // We'll use simple marker instead
            LCD_printString(">", 40, y_pos, 0, text_size);  // Arrow pointing to selection
        }
        
        LCD_printString((char*)menu_options[i], 70, y_pos, 0, text_size);
        LCD_printString((char*)menu_options[i], 71, y_pos, 0, text_size);
    }
    
    //bird
    LCD_Draw_Sprite_Scaled(35, 187, 12, 16, (const uint8_t *)menu_bird, 2);
    //LCD_printString("Bird", 45, 220, 1, 1);

    //paw
    draw_cat_paw(95, 185);
    //LCD_printString("Cat", 105, 220, 1, 1);

    //shark
    draw_loading_small_shark(187,205); 
    //LCD_printString("Shark", 165, 220, 1, 1);

    // Instructions
    LCD_printString("Press BT3", 50, 240, 1, 1);
    LCD_printString("Press BT3", 51, 240, 1, 1);
    
    LCD_Refresh(&cfg0);
}

// ==============================================
// PUBLIC API IMPLEMENTATION
// ==============================================

void Menu_Init(MenuSystem* menu) {
    menu->selected_option = 0;

    RGB_Init();
    RGB_SetGameColor(0); 
}

MenuState Menu_Run(MenuSystem* menu) {
    static Direction last_direction = CENTRE;  // Track last direction for debouncing
    MenuState selected_game = MENU_STATE_HOME;  // Which game was selected
    
    // Menu's own loop - runs until game is selected
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // Read input
        Input_Read();
        
        // Read current joystick position
        Joystick_Read(&joystick_cfg, &joystick_data);
        
        // Handle joystick navigation (up/down to select option)
        Direction current_direction = joystick_data.direction;
        
        if (current_direction == S && last_direction != S) {  // Joystick pushed DOWN
            // Move selection down
            menu->selected_option++;
            if (menu->selected_option >= NUM_MENU_OPTIONS) {
                menu->selected_option = 0;  // Wrap around
            }
            RGB_SetGameColor(menu->selected_option);
        } 
        else if (current_direction == N && last_direction != N) {  // Joystick pushed UP
            // Move selection up
            if (menu->selected_option == 0) {
                menu->selected_option = NUM_MENU_OPTIONS - 1;  // Wrap around
            } else {
                menu->selected_option--;
            }
            RGB_SetGameColor(menu->selected_option);
        }
        
        last_direction = current_direction;
        
        // Handle button press to select current option
        if (current_input.btn3_pressed) {
            // User pressed button - select the highlighted option
            if (menu->selected_option == 0) {
                selected_game = MENU_STATE_GAME_1;
            } else if (menu->selected_option == 1) {
                selected_game = MENU_STATE_GAME_2;
            } else if (menu->selected_option == 2) {
                selected_game = MENU_STATE_GAME_3;
            }
            break;  // Exit menu loop - game selected!
        }
        
        // Render menu
        render_home_menu(menu);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < MENU_FRAME_TIME_MS) {
            HAL_Delay(MENU_FRAME_TIME_MS - frame_time);
        }
    }
    
    return selected_game;  // Return which game was selected
}
