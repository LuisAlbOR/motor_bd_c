#define _XOPEN_SOURCE 700
#include "file_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void table_path(const char *table_name, char *out, size_t out_size) {
    snprintf(out, out_size, "data/%s.tbl", table_name);
}

int fm_table_exists(const char *table_name) {
    char path[256];
    table_path(table_name, path, sizeof(path));
    return access(path, F_OK) == 0;
}

int fm_create_table_file(const char *table_name) {
    char path[256];
    table_path(table_name, path, sizeof(path));
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) return -1;
    Page page;
    page_init(&page, 0);
    int ok = fm_write_page(fd, 0, &page);
    close(fd);
    return ok;
}

int fm_open_table(const char *table_name) {
    char path[256];
    table_path(table_name, path, sizeof(path));
    return open(path, O_CREAT | O_RDWR, 0644);
}

int fm_read_page(int fd, int page_id, Page *page) {
    if (!page) return -1;
    off_t offset = (off_t)page_id * PAGE_SIZE;
    ssize_t n = pread(fd, page, sizeof(Page), offset);
    if (n == 0) {
        page_init(page, page_id);
        return 0;
    }
    return n == sizeof(Page) ? 0 : -1;
}

int fm_write_page(int fd, int page_id, const Page *page) {
    if (!page) return -1;
    off_t offset = (off_t)page_id * PAGE_SIZE;
    ssize_t n = pwrite(fd, page, sizeof(Page), offset);
    fsync(fd);
    return n == sizeof(Page) ? 0 : -1;
}
