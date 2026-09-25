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
        INPUT_KEY_F5,
        INPUT_KEY_F7,
        INPUT_KEY_F8,
    INPUT_KEY_F9,
    INPUT_KEY_F10
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

typedef enum {
    INPUT_LAYOUT_US = 0,
    INPUT_LAYOUT_ABNT2 = 1
} InputLayout;

void input_init(int screen_width, int screen_height);
void input_keyboard_irq(uint8_t scancode);
void input_mouse_irq_byte(uint8_t byte);
/* USB tablet reports a position in 0..xmax / 0..ymax. PS/2 deltas are ignored. */
void input_use_absolute(int on);
void input_pointer_absolute(int x, int y, int xmax, int ymax, int buttons);
bool input_next_event(InputEvent *event);
void input_clear_events(void);
uint32_t input_lost_events(void);
InputMouse input_mouse_snapshot(void);
bool input_left_pressed(void);
void input_consume_left_press(void);

/* LEARN:F5P02
 * Set-1 identity: make code N is index N.
 * E0-prefixed make code N is index 128+N, so arrows do not collide
 * with the keypad. Up/left/right/down = 200/203/205/208. */
#define INPUT_SCAN_EXT 128
#define INPUT_SCAN_UP 200
#define INPUT_SCAN_LEFT 203
#define INPUT_SCAN_RIGHT 205
#define INPUT_SCAN_DOWN 208

void input_keystate_note(uint8_t scancode);
int input_key_down(int scancode);
void input_keystate_clear(void);
/* Screen-space deltas (positive x right, positive y down). Consumed. */
void input_mouse_delta(int *dx, int *dy);
void input_mouse_add(int dx, int dy, int buttons);
/* Capture belongs to a task id. -1 releases. Unfocused owners lose it. */
void input_capture_set(int task_id);
void input_capture_release_task(int task_id);
int input_capture_owner(void);
int input_mouse_delta_for(int task_id, int *dx, int *dy);
/* One sample shared by mouse_dx and mouse_dy. y_axis=0 returns dx. */
int input_mouse_axis(int task_id, int y_axis);
void input_set_layout(InputLayout layout);
InputLayout input_get_layout(void);
int input_load_layout_file(const char *path);
int input_save_layout_file(const char *path);


#endif
