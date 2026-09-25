int main() {
    int vs;
    int fs;
    int program;
    int loc;
    vs = shader_make(shader_vert(), "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }");
    fs = shader_make(shader_frag(), "out vec4 color; void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }");
    if (shader_ok(vs) == 0 || shader_ok(fs) == 0) {
        return 0;
    }
    program = prog_make();
    prog_attach(program, vs);
    prog_attach(program, fs);
    if (prog_link(program) != 0 || prog_ok(program) == 0) {
        return 0;
    }
    loc = prog_uniloc(program, "time");
    if (loc >= 0) {
        prog_setf(program, loc, 0, 0.0);
    }
    shader_drop(vs);
    shader_drop(fs);
    prog_drop(program);
    return 1;
}
