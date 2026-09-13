#include <stdint.h>

int mx, my;
int left_clicked, right_clicked, middle_clicked;
int current_byte = 0;
uint8_t bytes[4] = { 0 };
int mouse_speed = 7;
int mouse_possessed_task_id = 0;

int Scancode = -1;

#define TRUE 1
#define FALSE 0

#define pic1_command 0x20
#define pic1_data 0x21
#define pic2_command 0xa0
#define pic2_data 0xa1
#define icw1_def 0x10
#define icw1_icw4 0x01
#define icw4_x86 0x01

#define y_overflow      0b10000000
#define x_overflow      0b01000000
#define y_negative      0b00100000
#define x_negative      0b00010000
#define always_set      0b00001000
#define middle_click    0b00000100
#define right_click     0b00000010
#define left_click      0b00000001
#define unused_a        0b10000000
#define unused_b        0b01000000

void InitialiseIDT();
extern void LoadIDT();
void HandleISR1();
void HandleISR2();
void RemapPIC();

struct IDTElement {
    unsigned short lower;
    unsigned short selector;
    unsigned char zero;
    unsigned char flags;
    unsigned short higher;
};

struct IDTElement _idt[256];
extern unsigned int isr1, isr12;
unsigned int base, base12;

unsigned char inportb(unsigned short port) {
    unsigned char value;
    __asm__ __volatile__("inb %1, %0" : "=a" (value) : "dN" (port));
    return value;
}

void outportb(unsigned short port, unsigned char data) {
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (port), "a" (data));
}

void InitialiseIDT() {
    _idt[1].lower = (base & 0xffff);
    _idt[1].higher = (base >> 16) & 0xffff;
    _idt[1].selector = 0x08;
    _idt[1].zero = 0;
    _idt[1].flags = 0x8e;

    _idt[12].lower = (base12 & 0xffff);
    _idt[12].higher = (base12 >> 16) & 0xffff;
    _idt[12].selector = 0x08;
    _idt[12].zero = 0;
    _idt[12].flags = 0x8e;

    RemapPIC();

    outportb(0x21, 0b11111001);
    outportb(0xa1, 0x00);

    LoadIDT();
}

void RemapPIC() {
    unsigned char a, b;

    a = inportb(pic1_data);
    b = inportb(pic2_data);

    outportb(pic1_command, icw1_def | icw1_icw4);
    outportb(pic2_command, icw1_def | icw1_icw4);

    outportb(pic1_data, 0);
    outportb(pic2_data, 8);

    outportb(pic1_data, 4);
    outportb(pic2_data, 2);

    outportb(pic1_data, icw4_x86);
    outportb(pic2_data, icw4_x86);

    outportb(pic1_data, a);
    outportb(pic2_data, b);
}

void HandleISR1() {
    Scancode = inportb(0x60);
    outportb(0xa0, 0x20);
    outportb(0x20, 0x20);
}

void HandleMouseInterrupt();
void HandleISR12() {
    HandleMouseInterrupt();
    outportb(0xa0, 0x20);
    outportb(0x20, 0x20);
}

#define ps2_status_port 0x64
#define ps2_command_port 0x64
#define ps2_data_port 0x60
#define ps2_output_full 0x01
#define ps2_input_full 0x02
#define ps2_mouse_data 0x20
#define ps2_ack 0xfa
#define ps2_resend 0xfe

int PS2WaitWrite() {
    int timeout = 100000;

    while (timeout--) {
        if ((inportb(ps2_status_port) & ps2_input_full) == 0)
            return TRUE;
    }

    return FALSE;
}

int PS2WaitRead(unsigned char* value) {
    int timeout = 100000;

    while (timeout--) {
        if (inportb(ps2_status_port) & ps2_output_full) {
            *value = inportb(ps2_data_port);
            return TRUE;
        }
    }

    return FALSE;
}

int PS2WaitMouseRead(unsigned char* value) {
    int timeout = 100000;

    while (timeout--) {
        unsigned char status = inportb(ps2_status_port);

        if (status & ps2_output_full) {
            unsigned char data = inportb(ps2_data_port);

            if (status & ps2_mouse_data) {
                *value = data;
                return TRUE;
            }
        }
    }

    return FALSE;
}

void PS2FlushOutput() {
    int timeout = 1000;

    while (timeout-- && (inportb(ps2_status_port) & ps2_output_full))
        inportb(ps2_data_port);
}

int MouseCommand(unsigned char command) {
    unsigned char response;

    for (int attempt = 0; attempt < 3; attempt++) {
        if (!PS2WaitWrite())
            return FALSE;
        outportb(ps2_command_port, 0xd4);

        if (!PS2WaitWrite())
            return FALSE;
        outportb(ps2_data_port, command);

        if (!PS2WaitMouseRead(&response))
            return FALSE;

        if (response == ps2_ack)
            return TRUE;
        if (response != ps2_resend)
            return FALSE;
    }

    return FALSE;
}

void InitialiseMouse() {
    unsigned char controller_config;

    current_byte = 0;
    PS2FlushOutput();

    /* Enable the PS/2 auxiliary port. */
    if (!PS2WaitWrite())
        return;
    outportb(ps2_command_port, 0xa8);

    /* Enable IRQ12 and make sure the second-port clock is running. */
    if (!PS2WaitWrite())
        return;
    outportb(ps2_command_port, 0x20);
    if (!PS2WaitRead(&controller_config))
        return;

    controller_config |= 0x02;
    controller_config &= ~0x20;

    if (!PS2WaitWrite())
        return;
    outportb(ps2_command_port, 0x60);
    if (!PS2WaitWrite())
        return;
    outportb(ps2_data_port, controller_config);

    /* Defaults, then streaming. Each command must be acknowledged. */
    if (!MouseCommand(0xf6))
        return;
    if (!MouseCommand(0xf4))
        return;
}

void HandleMousePacket();
void HandleMouseInterrupt() {
    unsigned char status = inportb(ps2_status_port);

    if (!(status & ps2_output_full) || !(status & ps2_mouse_data))
        return;

    uint8_t byte = inportb(ps2_data_port);

    if (current_byte == 0 && !(byte & always_set))
        return;

    bytes[current_byte] = byte;
    current_byte++;

    if (current_byte >= 3)
        current_byte = 0;

    if (current_byte == 0)
        HandleMousePacket();
}

void HandleMousePacket() {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;

    uint8_t status = bytes[0];
    int32_t change_x = (int32_t) bytes[1];
    int32_t change_y = (int32_t) bytes[2];

    if (status & x_overflow || status & y_overflow)
        return;

    if (status & x_negative)
        change_x |= 0xffffff00;

    if (status & y_negative)
        change_y |= 0xffffff00;

    left_clicked = status & left_click;
    right_clicked = status & right_click;
    middle_clicked = status & middle_click;

    if (change_x > 0)
        mx += 2;
    else if (change_x < 0)
        mx -= 2;

    if (change_y > 0)
        my -= 2;
    else if (change_y < 0)
        my += 2;

    if (mx < 0)
        mx = 0;
    else if (mx > VBE->x_resolution)
        mx = VBE->x_resolution;
    
    if (my < 0)
        my = 0;
    else if (my > VBE->y_resolution)
        my = VBE->y_resolution;
    
}

int shift_pressed = FALSE;
int caps_pressed = FALSE;
int escape_pressed = FALSE;
int backspace_pressed = FALSE;
int alt_pressed = FALSE;
int ctrl_pressed = FALSE;
int enter_pressed = FALSE;

unsigned char ProcessScancode(int scancode) {
    if (scancode == 0x01)
        escape_pressed = TRUE;

    if (scancode ==  0x01)
        escape_pressed = TRUE;
        
    else if (scancode ==  0x02)
        if (shift_pressed == TRUE)
            return '!';
        else 
            return '1';
        
    else if (scancode ==  0x03)
        if (shift_pressed == TRUE)
            return '"';
        else 
            return '2';
        
    else if (scancode ==  0x04)
        if (shift_pressed == TRUE)
            return '£';
        else 
            return '3';
        
    else if (scancode ==  0x05)
        if (shift_pressed == TRUE)
            return '$';
        else 
            return '4';
            
    else if (scancode == 0x06)
        if (shift_pressed == TRUE)
            return '%';
        else 
            return '5';
        
    else if (scancode == 0x07)
        if (shift_pressed == TRUE)
            return '^';
        else 
            return '6';
        
    else if (scancode == 0x08)
        if (shift_pressed == TRUE)
            return '&';
        else 
            return '7';
        
    else if (scancode == 0x09)
        if (shift_pressed == TRUE)
            return '*';
        else 
            return '8';
        
    else if (scancode == 0x0A)
        if (shift_pressed == TRUE)
            return '(';
        else 
            return '9';
        
    else if (scancode == 0x0B)
        if (shift_pressed == TRUE)
            return ')';
        else 
            return '0';
        
    else if (scancode == 0x0C)
        if (shift_pressed == TRUE)
            return '_';
        else 
            return '-';
            
    else if (scancode == 0x0D)
        if (shift_pressed == TRUE)
            return '+';
        else 
            return '=';
        
    // Backspace
    else if (scancode == 0x0E)
        backspace_pressed = TRUE;
        
    else if (scancode == 0x0F)
        return '\t';
        
    else if (scancode == 0x10)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'Q';
        else 
            return 'q';
        
    else if (scancode == 0x11)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'W';
        else 
            return 'w';
        
    else if (scancode == 0x12)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'E';
        else 
            return 'e';
        
    else if (scancode == 0x13)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'R';
        else 
            return 'r';
        
    else if (scancode == 0x14)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'T';
        else 
            return 't';
        
    else if (scancode == 0x15)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'Y';
        else 
            return 'y';
        
    else if (scancode == 0x16)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'U';
        else 
            return 'u';
        
    else if (scancode == 0x17)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'I';
        else 
            return 'i';
        
    else if (scancode == 0x18)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'O';
        else 
            return 'o';
        
    else if (scancode == 0x19)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'P';
        else 
            return 'p';
        
    else if (scancode == 0x1A)
        if (shift_pressed == TRUE)
            return '{';
        else 
            return '[';
        
    else if (scancode == 0x1B)
        if (shift_pressed == TRUE)
            return '}';
        else 
            return ']';
        
    // enter pressed
    else if (scancode == 0x1C) {
        enter_pressed = TRUE;
        return '\n';
    }
        
    // ctrl pressed
    else if (scancode == 0x1D)
        ctrl_pressed = TRUE;
        
    else if (scancode == 0x1E)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'A';
        else 
            return 'a';
        
    else if (scancode == 0x1F)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'S';
        else 
            return 's';
        
    else if (scancode == 0x20)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'D';
        else 
            return 'd';
        
    else if (scancode == 0x21)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'F';
        else 
            return 'f';
        
    else if (scancode == 0x22)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'G';
        else 
            return 'g';
        
    else if (scancode == 0x23)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'H';
        else 
            return 'h';
        
    else if (scancode == 0x24)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'J';
        else 
            return 'j';
        
    else if (scancode == 0x25)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'K';
        else 
            return 'k';
        
    else if (scancode == 0x26)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'L';
        else 
            return 'l';
        
    else if (scancode == 0x27)
        if (shift_pressed == TRUE)
            return ':';
        else 
            return ';';
        
    else if (scancode == 0x28) 
        if (shift_pressed == TRUE)
            return '@';
        else 
            return '\'';
        
    else if (scancode == 0x29)
        if (shift_pressed == TRUE)
            return '¬';
        else 
            return '`';
        
    // shift pressed
    else if (scancode == 0x2A) {
        shift_pressed = TRUE;
        Scancode = -1;
    }
        
    else if (scancode == 0x2B)
        if (shift_pressed == TRUE)
            return '|';
        else 
            return '\\';
    
    else if (scancode == 0x2C)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'Z';
        else 
            return 'z';
    
    else if (scancode == 0x2D)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'X';
        else 
            return 'x';
        
    else if (scancode == 0x2E)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'C';
        else 
            return 'c';
        
    else if (scancode == 0x2F)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'V';
        else 
            return 'v';
        
    else if (scancode == 0x30)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'B';
        else 
            return 'b';
        
    else if (scancode == 0x31)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'N';
        else 
            return 'n';
        
    else if (scancode == 0x32)
        if (shift_pressed == TRUE || caps_pressed == TRUE)
            return 'M';
        else 
            return 'm';
        
    else if (scancode == 0x33)
        if (shift_pressed == TRUE)
            return '<';
        else 
            return ',';
        
    else if (scancode == 0x34)
        if (shift_pressed == TRUE)
            return '>';
        else 
            return '.';
    
    else if (scancode == 0x35)
        if (shift_pressed == TRUE)
            return '?';
        else 
            return '/';

    // shift pressed    
    else if (scancode == 0x36) {
        shift_pressed = TRUE;
        Scancode = -1;
    }

    // alt pressed
    else if (scancode == 0x38)
        alt_pressed = TRUE;
        
        
    else if (scancode == 0x39)
        return ' ';
    
    // Caps pressed
    else if (scancode == 0x3A) {
        if (caps_pressed == TRUE)
            caps_pressed = FALSE;
        else if (caps_pressed == FALSE)
            caps_pressed = TRUE;

        Scancode = -1;
    }

    // shift released
    if (scancode == 0xAA) {
        shift_pressed = FALSE;
        Scancode = -1;
    }

    // shift released
    if (scancode == 0xB6) {
        shift_pressed = FALSE;
        Scancode = -1;
    }

    return '\0';
 
}