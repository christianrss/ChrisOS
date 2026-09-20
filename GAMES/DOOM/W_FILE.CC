/* ChrisC WAD file: fopen/fseek/fread, int fds, layout matches wad_file_t. */

struct chris_wad {
    char *file_class;
    char *mapped;
    int length;
    int fd;
};

struct chris_wad *W_OpenFile(char *path) {
    struct chris_wad *w;
    int fd;
    fd = fopen(path);
    if (fd < 0)
        return 0;
    w = malloc(32);
    if (!w) {
        fclose(fd);
        return 0;
    }
    w->file_class = 0;
    w->mapped = 0;
    w->length = fsize(path);
    w->fd = fd;
    return w;
}

void W_CloseFile(struct chris_wad *wad) {
    if (!wad)
        return;
    fclose(wad->fd);
    free(wad);
}

int W_Read(struct chris_wad *wad, int offset, char *buffer, int buffer_len) {
    if (!wad)
        return 0;
    fseek(wad->fd, offset);
    return fread(wad->fd, buffer, buffer_len);
}

int W_FileLength(struct chris_wad *wad) {
    if (!wad)
        return 0;
    return wad->length;
}
