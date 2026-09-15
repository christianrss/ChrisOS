#include "input.h"

#define INPUT_QUEUE_CAPACITY 64u

typedef struct {
    volatile uint32_t version;
    volatile int x;
    volatile int y;
    volatile bool left_down;
    volatile bool right_down;
    volatile bool middle_down;
    volatile uint32_t left_press_sequence;
} IrqMouseState;

static InputEvent g_queue[INPUT_QUEUE_CAPACITY];
static volatile uint32_t g_queue_head;
static volatile uint32_t g_queue_tail;
static volatile uint32_t g_lost_events;

static IrqMouseState g_mouse;
static uint32_t g_seen_left_press;
static int g_screen_width;
static int g_screen_height;
static uint8_t g_mouse_packet[3];
static unsigned int g_mouse_packet_index;

static bool g_extended;
static bool g_left_shift;
static bool g_right_shift;
static bool g_caps_lock;

static const char normal_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static const char shifted_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?'
};

static void compiler_barrier(void) {
    __asm__ volatile ("" ::: "memory");
}

static void queue_push(InputEvent event) {
    uint32_t head = g_queue_head;
    uint32_t next = (head + 1u) % INPUT_QUEUE_CAPACITY;

    if (next == g_queue_tail) {
        ++g_lost_events;
        return;
    }
    g_queue[head] = event;
    compiler_barrier();
    g_queue_head = next;
}

static void push_key(InputKey key) {
    InputEvent event;
    event.type = INPUT_EVENT_KEY;
    event.key = key;
    event.character = '\0';
    queue_push(event);
}

static void push_text(char character) {
    InputEvent event;
    event.type = INPUT_EVENT_TEXT;
    event.key = INPUT_KEY_NONE;
    event.character = character;
    queue_push(event);
}

static InputKey extended_key(uint8_t code) {
    switch (code) {
        case 0x47: return INPUT_KEY_HOME;
        case 0x48: return INPUT_KEY_UP;
        case 0x4B: return INPUT_KEY_LEFT;
        case 0x4D: return INPUT_KEY_RIGHT;
        case 0x4F: return INPUT_KEY_END;
        case 0x50: return INPUT_KEY_DOWN;
        case 0x53: return INPUT_KEY_DELETE;
        default: return INPUT_KEY_NONE;
    }
}

static InputKey plain_key(uint8_t code) {
    switch (code) {
        case 0x01: return INPUT_KEY_ESCAPE;
        case 0x0E: return INPUT_KEY_BACKSPACE;
        case 0x0F: return INPUT_KEY_TAB;
        case 0x1C: return INPUT_KEY_ENTER;
        default: return INPUT_KEY_NONE;
    }
}

void input_init(int screen_width, int screen_height) {
    g_queue_head = 0;
    g_queue_tail = 0;
    g_lost_events = 0;
    g_screen_width = screen_width > 0 ? screen_width : 1;
    g_screen_height = screen_height > 0 ? screen_height : 1;
    g_mouse.version = 0;
    g_mouse.x = g_screen_width / 2;
    g_mouse.y = g_screen_height / 2;
    g_mouse.left_down = false;
    g_mouse.right_down = false;
    g_mouse.middle_down = false;
    g_mouse.left_press_sequence = 0;
    g_seen_left_press = 0;
    g_mouse_packet_index = 0;
    g_extended = false;
    g_left_shift = false;
    g_right_shift = false;
    g_caps_lock = false;
}

void input_keyboard_irq(uint8_t scancode) {
    bool released;
    uint8_t code;
    bool shifted;
    char character;
    InputKey key;

    if (scancode == 0xE0u) {
        g_extended = true;
        return;
    }

    released = (scancode & 0x80u) != 0;
    code = (uint8_t)(scancode & 0x7Fu);

    if (!g_extended && code == 0x2A) {
        g_left_shift = !released;
        return;
    }
    if (!g_extended && code == 0x36) {
        g_right_shift = !released;
        return;
    }

    if (g_extended) {
        key = extended_key(code);
        g_extended = false;
        if (!released && key != INPUT_KEY_NONE) {
            push_key(key);
        }
        return;
    }

    if (released) {
        return;
    }
    if (code == 0x3A) {
        g_caps_lock = !g_caps_lock;
        return;
    }

    key = plain_key(code);
    if (key != INPUT_KEY_NONE) {
        push_key(key);
        return;
    }

    shifted = g_left_shift || g_right_shift;
    character = normal_map[code];
    if (character >= 'a' && character <= 'z') {
        if (shifted != g_caps_lock) {
            character = (char)(character - 'a' + 'A');
        }
    } else if (shifted && shifted_map[code] != '\0') {
        character = shifted_map[code];
    }
    if (character != '\0') {
        push_text(character);
    }
}

static int clamp_int(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static void apply_mouse_packet(void) {
    uint8_t flags = g_mouse_packet[0];
    int dx;
    int dy;
    bool left;

    if ((flags & 0xC0u) != 0) {
        return;
    }

    dx = (int)(int8_t)g_mouse_packet[1];
    dy = (int)(int8_t)g_mouse_packet[2];
    left = (flags & 0x01u) != 0;

    ++g_mouse.version;
    compiler_barrier();
    g_mouse.x = clamp_int(g_mouse.x + dx, 0, g_screen_width - 1);
    g_mouse.y = clamp_int(g_mouse.y - dy, 0, g_screen_height - 1);
    if (left && !g_mouse.left_down) {
        ++g_mouse.left_press_sequence;
    }
    g_mouse.left_down = left;
    g_mouse.right_down = (flags & 0x02u) != 0;
    g_mouse.middle_down = (flags & 0x04u) != 0;
    compiler_barrier();
    ++g_mouse.version;
}

void input_mouse_irq_byte(uint8_t byte) {
    if (g_mouse_packet_index == 0 && (byte & 0x08u) == 0) {
        return;
    }

    g_mouse_packet[g_mouse_packet_index++] = byte;
    if (g_mouse_packet_index == 3) {
        g_mouse_packet_index = 0;
        apply_mouse_packet();
    }
}

bool input_next_event(InputEvent *event) {
    uint32_t tail;

    if (event == 0) {
        return false;
    }
    tail = g_queue_tail;
    if (tail == g_queue_head) {
        return false;
    }
    compiler_barrier();
    *event = g_queue[tail];
    compiler_barrier();
    g_queue_tail = (tail + 1u) % INPUT_QUEUE_CAPACITY;
    return true;
}

void input_clear_events(void) {
    compiler_barrier();
    g_queue_tail = g_queue_head;
}

uint32_t input_lost_events(void) {
    return g_lost_events;
}

InputMouse input_mouse_snapshot(void) {
    InputMouse result;
    uint32_t before;
    uint32_t after;

    do {
        before = g_mouse.version;
        compiler_barrier();
        result.x = g_mouse.x;
        result.y = g_mouse.y;
        result.left_down = g_mouse.left_down;
        result.right_down = g_mouse.right_down;
        result.middle_down = g_mouse.middle_down;
        result.left_press_sequence = g_mouse.left_press_sequence;
        compiler_barrier();
        after = g_mouse.version;
    } while ((before & 1u) != 0 || before != after);

    return result;
}

bool input_left_pressed(void) {
    InputMouse mouse = input_mouse_snapshot();
    return mouse.left_press_sequence != g_seen_left_press;
}

void input_consume_left_press(void) {
    InputMouse mouse = input_mouse_snapshot();
    g_seen_left_press = mouse.left_press_sequence;
}
