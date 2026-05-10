/**
 * @file Game_1.c
 * @brief Flappy Bird game for STM32 Nucleo-L476RG
 */

#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_gpio.h"
#include <stdio.h>
#include <stdlib.h>


//variables
extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

//constant
#define FRAME       30
#define SCREEN_W    240    //width
#define SCREEN_H    240    //height

#define BIRD_X      30
#define BIRD_SIZE   16
#define GRAVITY     0.3f
#define JUMP_POWER  -5.0f  //upward

#define PIPE_WIDTH          15
#define PIPE_COLOR_GREEN    3
#define WIN_SCORE           30//test 15

#define BT3_PORT GPIOC
#define BT3_PIN GPIO_PIN_3

#define RESTART_PORT GPIOC
#define RESTART_PIN GPIO_PIN_2




// bird
static const uint8_t bird[16][16] = {
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


// enumeration

enum GameState{
    LOADING,    
    COVER,      
    DIFFICULTY, 
    READY,     
    PLAYING,    
    RESULT     
};

enum Difficulty{
    EASY = 0,
    MID = 1,
    ENDLESS = 2
};

#define RESTART_GAME 0
#define BACK_TO_MENU 1


//global variables

static enum GameState state = LOADING;  //defult: loading, easy
static enum Difficulty difficulty = EASY;

static int bird_y = 120;
static float bird_velocity = 0;
static int last_up = 0;

static int pipe_x = SCREEN_W;
static int pipe_gap_y = 80;
static int pipe_speed = 3;
static int pipe_gap = 60;

static int score = 0;
static int high_score[3] = {0, 0, 0};
static int pipe_pass = 0;
static int score_bj = 0;    //last changed score

static int buzzer_timer = 0;   //frames remained for buzzer

static int cover_sel = 0;       //0=start, 1=back
static int diff_sel = 0;        //easy mid endless
static int result_sel = RESTART_GAME;    //restart back

static int last_joy_up = 0;
static int last_joy_down = 0;
static int last_btn3 = 0;


//functions

static int force_back(void) {
    if (HAL_GPIO_ReadPin(RESTART_PORT, RESTART_PIN) == GPIO_PIN_RESET) {
        HAL_Delay(50);
        return 1;
    }
    return 0;
}

static void sound(int f, int t) {
    buzzer_tone(&buzzer_cfg, f, 30);
    buzzer_timer = t;
}

static void music(void) {
    buzzer_off(&buzzer_cfg);
    buzzer_timer = 0;
    
    uint16_t note[] = {262, 262, 392, 392, 440, 440, 392};  //1155665
    uint32_t time[] = {400, 400, 400, 400, 400, 400, 800};
    
    int noteCount = sizeof(note) / sizeof(note[0]);
    
    for (int i = 0; i < noteCount; i++) {
        buzzer_tone(&buzzer_cfg, note[i], 30);
        HAL_Delay(time[i]);
        buzzer_off(&buzzer_cfg);
    }
}

static void win_music(void) {
    buzzer_off(&buzzer_cfg);
    buzzer_timer = 0;
    
    uint16_t note[] = {392, 349,392, 349, 392, 349,392, 588, 524, 392, 588, 524};   //SHENSELF
    uint32_t time[] = {200, 200, 200, 200, 200, 200, 200,200,400,200,200,400};
    
    int noteCount = sizeof(note) / sizeof(note[0]);
    
    for (int i = 0; i < noteCount; i++) {
        buzzer_tone(&buzzer_cfg, note[i], 30);
        HAL_Delay(time[i]);
        buzzer_off(&buzzer_cfg);
    }
}

static void cloud(int x, int y) {
    LCD_Draw_Circle(x, y, 15, 1, 1);      
    LCD_Draw_Circle(x - 12, y, 15, 1, 1); 
    LCD_Draw_Circle(x + 12, y, 15, 1, 1); 
    LCD_Draw_Circle(x - 6, y-10, 10, 1, 1);  
    LCD_Draw_Circle(x + 6, y-10, 10, 1, 1);  
}

static void reset(void) {
    bird_y = 120;
    bird_velocity = 0;
    last_up = 0;
    pipe_x = SCREEN_W;
    pipe_gap_y = 80;
    score = 0;
    pipe_pass = 0;
    score_bj = 0;

    switch (difficulty) {
        case EASY: pipe_speed = 2; pipe_gap = 75; break;
        case MID: pipe_speed = 3; pipe_gap = 60; break;
        case ENDLESS: pipe_speed = 3; pipe_gap = 60; break;
    }
}

static void highest(void) {
    int num = difficulty;  //EASY=0, MID=1, ENDLESS=2
    if (score > high_score[num]) {
        high_score[num] = score;
    }
}

static void pipe(void) {
    LCD_Draw_Rect(pipe_x, 0, PIPE_WIDTH, pipe_gap_y, PIPE_COLOR_GREEN, 1);
    LCD_Draw_Rect(pipe_x, pipe_gap_y + pipe_gap, PIPE_WIDTH,
                  SCREEN_H - (pipe_gap_y + pipe_gap), PIPE_COLOR_GREEN, 1);
}


static void loading(int progress) {
    LCD_Fill_Buffer(11);
    
    //progress bar
    LCD_printString("Loading...", 45, 30, 1, 3);
    
    LCD_Draw_Rect(20, 80, 200, 20, 1, 0);
    
    int fill_width = (progress*200)/100;
    if (fill_width > 0) {
        LCD_Draw_Rect(20, 80, fill_width, 20, 8, 1);
    }
    
    char percent[10];
    sprintf(percent, "%d%%", progress);
    LCD_printString(percent, 100, 85, 1, 1);
    
    LCD_printString("How to Play:", 20, 130, 1, 2);
    LCD_printString("Push joystick up", 20, 160, 1, 1);
    LCD_printString("Make the bird fly", 20, 175, 1, 1);
    LCD_printString("through pipes", 20, 195, 1, 1);
    LCD_printString("to get scores!", 20, 210, 1, 1);
    
    LCD_Refresh(&cfg0);
}

static void cover(void) {
    LCD_Fill_Buffer(11);
    
    //title
    LCD_printString("Flappy Bird", 30, 35, 1, 3);
    
    //bird
    LCD_Draw_Sprite_Scaled(108, 80, 12, 16, (const uint8_t *)bird, 2);
    
    //cloud
    cloud(60,90);
    cloud(180,100);

    // menu
    if (cover_sel == 0) {
        LCD_printString("> Start", 65, 125, 1, 2);
    } else {
        LCD_printString("  Start", 65, 125, 1, 2);
    }
    
    if (cover_sel == 1) {
        LCD_printString("> Back", 65, 155, 1, 2);
    } else {
        LCD_printString("  Back", 65, 155, 1, 2);
    }
    
    //instruction
    LCD_printString("UP/DOWN BT3 To Confirm", 20, SCREEN_H - 30, 1, 1);
    
    LCD_Refresh(&cfg0);
}

static void difficultyselect(void) {
    LCD_Fill_Buffer(11);
    LCD_printString("Difficulty", 35, 30, 1, 3);
    
    if (diff_sel == 0) {
        LCD_printString("> Easy", 60, 90, 1, 2);
    } else {
        LCD_printString("  Easy", 60, 90, 1, 2);
    }

    if (diff_sel == 1) {
        LCD_printString("> Mid", 60, 125, 1, 2);
    } else {
        LCD_printString("  Mid", 60, 125, 1, 2);
    }

    if (diff_sel == 2) {
        LCD_printString("> Endless", 60, 160, 1, 2);
    } else {
        LCD_printString("  Endless", 60, 160, 1, 2);
    }
    
    LCD_printString("UP/DOWN BT3 To Confirm", 20, SCREEN_H - 30, 1, 1);
    LCD_Refresh(&cfg0);
}

static void ready(void) {

    LCD_Fill_Buffer(11);
    LCD_printString("Get Ready!", 35, 100, 1, 3);
    LCD_printString("Press BT3 to Start", 25, 160, 1, 2);
    LCD_Refresh(&cfg0);
}

static void result(void) {

    int win = (score >= WIN_SCORE && difficulty != ENDLESS);
    
    LCD_Fill_Buffer(11);

    cloud(200, 100);

    if (win == 1) {
        LCD_printString("YOU WIN!", 45, 45, 1, 3);
    } else {
        LCD_printString("GAME OVER", 45, 45, 1, 3);
    }
    
    char final[30], hs[30];
    int num = difficulty;
    sprintf(final, "Score: %d", score);
    sprintf(hs, "Best:%d", high_score[num]);

    LCD_printString(final, 55, 100, 1, 2);
    LCD_printString(hs, 55, 130, 1, 2);
    
    if (result_sel == 0) {
        LCD_printString("> Restart", 55, 175, 1, 2);
    } else {
        LCD_printString("  Restart", 55, 175, 1, 2);
    }

    if (result_sel == 1) {
        LCD_printString("> Back", 55, 205, 1, 2);
    }else {
        LCD_printString("  Back", 55, 205, 1, 2);
    }
    
    LCD_Refresh(&cfg0);
}

static void game(void) {
    
    LCD_Fill_Buffer(11);

    cloud(60, 90);
    cloud(180, 100);
    
    pipe();
    
    
    LCD_Draw_Sprite(BIRD_X, bird_y, 12, 16, (const uint8_t *)bird);
    
    
    char score_str[20], hs_str[20];
    int num = difficulty;
    sprintf(score_str, "Score:%d", score);
    sprintf(hs_str, "Best: %d", high_score[num]);
    LCD_printString(score_str, 10, 10, 1, 1);
    LCD_printString(hs_str, 10, 25, 1, 1);
   
    LCD_Refresh(&cfg0);
}



MenuState Game1_Run(void) {
    buzzer_tone(&buzzer_cfg, 1200, 30);
    HAL_Delay(50);
    buzzer_off(&buzzer_cfg);

    srand(HAL_GetTick());
    reset();
    
    //initialise
    state = LOADING;
    cover_sel = 0;
    diff_sel = 0;
    result_sel = RESTART_GAME;
    last_joy_up = 0;
    last_joy_down = 0;
    last_btn3 = 0;
    

    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        
        if (buzzer_timer > 0) {
            buzzer_timer-=1;
            if (buzzer_timer == 0){ 
                buzzer_off(&buzzer_cfg);
            }
        }
        
        int joy_up = (joystick_data.direction == N);
        int joy_down = (joystick_data.direction == S);
        int up_edge = joy_up && !last_joy_up;
        int down_edge = joy_down && !last_joy_down;
        last_joy_up = joy_up;
        last_joy_down = joy_down;
        
        int btn3_current = (HAL_GPIO_ReadPin(BT3_PORT, BT3_PIN) == GPIO_PIN_RESET);
        int btn3_edge = btn3_current && !last_btn3;
        last_btn3 = btn3_current;
        
        
        if (force_back()) {
            buzzer_off(&buzzer_cfg);
            state = COVER;
            cover_sel = 0;
            //break;
        }

        switch (state) {
            
            
            case LOADING:
                for (int progress = 0; progress <= 100; progress += 20) {
                    loading(progress);
                    HAL_Delay(150);
                }
                music();
                state = COVER;
                cover_sel = 0;
                break;
            
            
            case COVER:
                cover();
                
                if (up_edge || down_edge) {
                    cover_sel = 1 - cover_sel;
                    sound(1000, 2);  //navigation sound
                }
                
                if (btn3_edge) {
                    sound(1300, 3);   //confirm
                    if (cover_sel == 0) {
                        state = DIFFICULTY;
                        diff_sel = 0;
                    } else {
                        buzzer_off(&buzzer_cfg);
                        return MENU_STATE_HOME;
                    }
                }
                break;
            
            
            case DIFFICULTY:
                difficultyselect();
                
                if (up_edge) {
                    diff_sel-=1;
                    if (diff_sel < 0){ 
                        diff_sel = 2;
                    }
                    sound(1000, 2);
                }
                if (down_edge) {
                    diff_sel+=1;
                    if (diff_sel > 2){ 
                        diff_sel = 0;
                    }
                    sound(1000, 2);
                }
                
                if (btn3_edge) {
                    sound(1300, 3);
                    difficulty = diff_sel;
                    reset();
                    state = READY;
                }
                break;
            
            
            case READY:
                ready();
                
                if (btn3_edge) {
                    sound(1300, 3);
                    state = PLAYING;
                }
                break;
            
            
            case PLAYING:
                {
                    
                    int current_up = (joystick_data.direction == N);
                    if (current_up && !last_up) {
                        bird_velocity = JUMP_POWER;
                        sound(2000, 5);    //jump
                    }
                    last_up = current_up;
                    
                    bird_velocity += GRAVITY;
                    bird_y += (int)bird_velocity;
                    
                    //pipes move
                    pipe_x -= pipe_speed;
                    if (pipe_x < -PIPE_WIDTH) {
                        pipe_x = SCREEN_W;
                        pipe_gap_y = 40 + (rand() % 120);
                        pipe_pass = 0;
                    }
                    
                    //score
                    if (!pipe_pass && BIRD_X > pipe_x + PIPE_WIDTH) {
                        score+=1;
                        pipe_pass = 1;
                        sound(1500, 3);
                        
                        //dynamic difficulty
                        if ((score > 0) && (score % 5 == 0) && (score != score_bj)) {
                            score_bj = score;  // every 5 scores add speed and decrease pipe gap
                            pipe_speed+=1;


                            //limitation, otherwise error in endless state
                            if (pipe_speed > 8){ 
                                pipe_speed = 8;
                            }
                            pipe_gap-=5;
                            if (pipe_gap < 30){ 
                                pipe_gap = 30;
                            }
                        }
                        
                        //win condition
                        if (difficulty != ENDLESS && score >= WIN_SCORE) {
                            highest();
                            result_sel = RESTART_GAME;
                            state = RESULT;
                            win_music();
                            break;
                        }
                    }
                    
                    // collision detection
                    int collision = 0;
                    if (bird_y < 0 || (bird_y > SCREEN_H - BIRD_SIZE)){
                        collision = 1;
                    }
                    if (BIRD_X + BIRD_SIZE > pipe_x && BIRD_X < pipe_x + PIPE_WIDTH) {
                        if ((bird_y < pipe_gap_y) || (bird_y + BIRD_SIZE > pipe_gap_y + pipe_gap)) {
                            collision = 1;
                        }
                    }
                    
                    if (collision) {
                        highest();
                        sound(500, 10);
                        result_sel = RESTART_GAME;
                        state = RESULT;
                        break;
                    }
                    
                    game();
                }
                break;
            
            
            case RESULT:
                result();
                
                if (up_edge || down_edge) {
                    result_sel = 1 - result_sel;
                    sound(1000, 2);
                }
                
                if (btn3_edge) {
                    sound(1300, 3);
                    if (result_sel == RESTART_GAME) {
                        state = COVER;
                        cover_sel = 0;
                    } else {
                        buzzer_off(&buzzer_cfg);
                        return MENU_STATE_HOME;
                    }
                }
                break;
        }
        
        uint32_t dt = HAL_GetTick() - frame_start;
        if (dt < FRAME) HAL_Delay(FRAME - dt);
    }
}