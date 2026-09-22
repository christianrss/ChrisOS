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
    int len;
    fd = fopen(path);
    if (fd < 0)
        return 0;
    len = fsize(path);
    printf("W_OpenFile %s fd=%d len=%d\n", path, fd, len);
    w = malloc(32);
    if (!w) {
        fclose(fd);
        return 0;
    }
    w->file_class = 0;
    w->mapped = 0;
    w->length = len > 0 ? len : 0;
    if (w->length == 0) {
        /* fsize can fail on ./paths; fall back to known WAD size via read. */
        printf("W_OpenFile warn fsize=%d\n", len);
    }

void W_CloseFile(struct chris_wad *wad) {
    if (!wad)
        return;
    fclose(wad->fd);
    free(wad);
}

int W_Read(struct chris_wad *wad, int offset, char *buffer, int buffer_len) {
    int n;
    if (!wad)
        return 0;
    fseek(wad->fd, offset);
    n = fread(wad->fd, buffer, buffer_len);
    if (offset == 0 && buffer_len >= 4) {
        printf("W_Read hdr %d bytes: %c%c%c%c\n", n, buffer[0], buffer[1],
               buffer[2], buffer[3]);
    }
    return n;
}

int W_FileLength(struct chris_wad *wad) {
    if (!wad)
        return 0;
    return wad->length;
}
