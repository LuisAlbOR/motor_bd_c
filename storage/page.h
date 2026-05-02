#ifndef PAGE_H
#define PAGE_H

#include "../transaction/mvcc.h"

#define PAGE_SIZE 4096
#define PAGE_DATA_SIZE (PAGE_SIZE - (int)(sizeof(int) * 2))
#define MVCC_RECORD_VALUE_SIZE 240

typedef struct {
    MVCCHeader mvcc;
    char value[MVCC_RECORD_VALUE_SIZE];
} MVCCRecord;

typedef struct {
    int page_id;
    int num_records;
    char data[PAGE_DATA_SIZE];
} Page;

void page_init(Page *page, int page_id);

int page_append_record(Page *page, const char *record);
void page_print_records(const Page *page);

int page_append_mvcc_record(Page *page, const char *record, int tx_id);
void page_print_mvcc_records(const Page *page, const MVCCSnapshot *snapshot);

int page_mark_delete_mvcc_record(Page *page, const char *record, int tx_id, const MVCCSnapshot *snapshot);

#endif