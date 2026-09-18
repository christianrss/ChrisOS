#ifndef CHRIS_INPUT_H
#define CHRIS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_TEXT,
    INPUT_EVENT_KEY
} InputEventType;

typedef enum {
    INPUT_KEY_NONE = 0,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_TAB,
    INPUT_KEY_ENTER,
    INPUT_KEY_ESCAPE,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_HOME,
    INPUT_KEY_END,
    INPUT_KEY_DELETE,
    INPUT_KEY_F2,
    INPUT_KEY_F3,
    INPUT_KEY_F4,
    INPUT_KEY_F5
} InputKey;

typedef struct {
    InputEventType type;
    InputKey key;
    char character;
} InputEvent;

typedef struct {
    int x;
    int y;
    bool left_down;
    bool right_down;
    bool middle_down;
    uint32_t left_press_sequence;
} InputMouse;

void input_init(int screen_width, int screen_height);
void input_keyboard_irq(uint8_t scancode);
void input_mouse_irq_byte(uint8_t byte);
bool input_next_event(InputEvent *event);
void input_clear_events(void);
uint32_t input_lost_events(void);
InputMouse input_mouse_snapshot(void);
bool input_left_pressed(void);
void input_consume_left_press(void);

/* LEARN:F5P02 */
void input_keystate_note(uint8_t scancode);
int input_key_down(int scancode);
void input_keystate_clear(void);


#endif
