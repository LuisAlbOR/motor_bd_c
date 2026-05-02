#include "buffer_pool.h"
#include "../storage/file_manager.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

void buffer_init(BufferPool *bp) {
    if (!bp) return;

    memset(bp, 0, sizeof(BufferPool));
    bp->clock = 1;
}

static int find_frame(BufferPool *bp, int fd, int page_id) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (bp->frames[i].valid &&
            bp->frames[i].table_fd == fd &&
            bp->frames[i].page_id == page_id) {
            return i;
        }
    }

    return -1;
}

static int choose_victim_lru(BufferPool *bp) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (!bp->frames[i].valid) {
            return i;
        }
    }

    unsigned long oldest = ULONG_MAX;
    int victim = -1;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (bp->frames[i].pin_count == 0 &&
            bp->frames[i].last_used < oldest) {
            oldest = bp->frames[i].last_used;
            victim = i;
        }
    }

    return victim;
}

static int flush_frame(BufferFrame *frame) {
    if (!frame || !frame->valid) return 0;

    if (frame->is_dirty) {
        if (fm_write_page(frame->table_fd, frame->page_id, &frame->page) != 0) {
            return -1;
        }

        frame->is_dirty = 0;
    }

    return 0;
}

Page* buffer_fetch_page(BufferPool *bp, int fd, int page_id) {
    if (!bp) return NULL;

    int frame_index = find_frame(bp, fd, page_id);

    if (frame_index != -1) {
        BufferFrame *frame = &bp->frames[frame_index];

        frame->pin_count++;
        frame->last_used = bp->clock++;

        return &frame->page;
    }

    int victim = choose_victim_lru(bp);

    if (victim == -1) {
        fprintf(stderr, "BufferPool: no hay frames disponibles; todas las paginas estan fijadas con pin_count > 0.\n");
        return NULL;
    }

    BufferFrame *frame = &bp->frames[victim];

    if (flush_frame(frame) != 0) {
        fprintf(stderr, "BufferPool: error al escribir pagina dirty antes de reemplazar.\n");
        return NULL;
    }

    if (fm_read_page(fd, page_id, &frame->page) != 0) {
        fprintf(stderr, "BufferPool: error al leer page_id=%d desde disco.\n", page_id);
        return NULL;
    }

    frame->valid = 1;
    frame->table_fd = fd;
    frame->page_id = page_id;
    frame->is_dirty = 0;
    frame->pin_count = 1;
    frame->last_used = bp->clock++;

    return &frame->page;
}

int buffer_unpin_page(BufferPool *bp, int fd, int page_id, int is_dirty) {
    if (!bp) return -1;

    int frame_index = find_frame(bp, fd, page_id);

    if (frame_index == -1) {
        fprintf(stderr, "BufferPool: no se puede hacer unpin; pagina no encontrada.\n");
        return -1;
    }

    BufferFrame *frame = &bp->frames[frame_index];

    if (frame->pin_count <= 0) {
        fprintf(stderr, "BufferPool: pin_count ya es 0.\n");
        return -1;
    }

    frame->pin_count--;

    if (is_dirty) {
        frame->is_dirty = 1;
    }

    frame->last_used = bp->clock++;

    return 0;
}

int buffer_mark_dirty(BufferPool *bp, int fd, int page_id) {
    if (!bp) return -1;

    int frame_index = find_frame(bp, fd, page_id);

    if (frame_index == -1) {
        fprintf(stderr, "BufferPool: no se puede marcar dirty; pagina no encontrada.\n");
        return -1;
    }

    bp->frames[frame_index].is_dirty = 1;
    bp->frames[frame_index].last_used = bp->clock++;

    return 0;
}

int buffer_flush_page(BufferPool *bp, int fd, int page_id) {
    if (!bp) return -1;

    int frame_index = find_frame(bp, fd, page_id);

    if (frame_index == -1) {
        return 0;
    }

    return flush_frame(&bp->frames[frame_index]);
}
int buffer_invalidate_fd(BufferPool *bp, int fd) {
    if (!bp) return -1;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (bp->frames[i].valid && bp->frames[i].table_fd == fd) {

            if (bp->frames[i].is_dirty) {
                fm_write_page(
                    bp->frames[i].table_fd,
                    bp->frames[i].page_id,
                    &bp->frames[i].page
                );
            }

            bp->frames[i].valid = 0;
            bp->frames[i].is_dirty = 0;
            bp->frames[i].pin_count = 0;
            bp->frames[i].table_fd = -1;
            bp->frames[i].page_id = -1;
        }
    }

    return 0;
}

int buffer_flush_all(BufferPool *bp) {
    if (!bp) return -1;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (bp->frames[i].valid) {
            if (flush_frame(&bp->frames[i]) != 0) {
                return -1;
            }
        }
    }

    return 0;
}