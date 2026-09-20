/* Keyboard → D_PostEvent. ENGINE.LST only (needs d_event.c). */

struct dg_event {
    int type;
    int data1;
    int data2;
    int data3;
    int data4;
};

void D_PostEvent(struct dg_event *ev);

void post_key(int down, int code) {
    struct dg_event ev;
    if (down)
        ev.type = 0;
    else
        ev.type = 1;
    ev.data1 = code;
    ev.data2 = code;
    ev.data3 = 0;
    ev.data4 = 0;
    D_PostEvent(&ev);
}

void poll_key(int sc, int code) {
    int down;
    down = key(sc);
    if (down && !key_was[sc])
        post_key(1, code);
    if (!down && key_was[sc])
        post_key(0, code);
    key_was[sc] = down;
}

void I_StartTic() {
    poll_key(1, 27);
    poll_key(28, 13);
    poll_key(57, 32);
    poll_key(17, 119);
    poll_key(31, 115);
    poll_key(30, 97);
    poll_key(32, 100);
    poll_key(72, 173);
    poll_key(80, 175);
    poll_key(75, 172);
    poll_key(77, 174);
}
