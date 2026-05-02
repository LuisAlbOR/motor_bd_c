#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include "../storage/page.h"

#define BUFFER_SIZE 10

typedef struct {
    int valid;
    int table_fd;
    int page_id;
    int is_dirty;
    int pin_count;
    unsigned long last_used;
    Page page;
} BufferFrame;

typedef struct {
    BufferFrame frames[BUFFER_SIZE];
    unsigned long clock;
} BufferPool;

void buffer_init(BufferPool *bp);

Page* buffer_fetch_page(BufferPool *bp, int fd, int page_id);

int buffer_unpin_page(BufferPool *bp, int fd, int page_id, int is_dirty);
int buffer_mark_dirty(BufferPool *bp, int fd, int page_id);

int buffer_flush_page(BufferPool *bp, int fd, int page_id);
int buffer_flush_all(BufferPool *bp);
int buffer_invalidate_fd(BufferPool *bp, int fd);
#endif