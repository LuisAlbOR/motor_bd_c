#include "page.h"
#include <stdio.h>
#include <string.h>

void page_init(Page *page, int page_id) {
    if (!page) return;

    page->page_id = page_id;
    page->num_records = 0;
    memset(page->data, 0, sizeof(page->data));
}

/* Compatibilidad anterior */
int page_append_record(Page *page, const char *record) {
    return page_append_mvcc_record(page, record, 0);
}

void page_print_records(const Page *page) {
    MVCCSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    snapshot.tx_id = 0;
    snapshot.xmin = 0;
    snapshot.xmax = 999999;

    page_print_mvcc_records(page, &snapshot);
}

/* Nuevo INSERT con MVCC */
int page_append_mvcc_record(Page *page, const char *record, int tx_id) {
    if (!page || !record) return -1;

    int offset = page->num_records * (int)sizeof(MVCCRecord);

    if (offset + (int)sizeof(MVCCRecord) > PAGE_DATA_SIZE) {
        return -1;
    }

    MVCCRecord mvcc_record;
    memset(&mvcc_record, 0, sizeof(MVCCRecord));

    mvcc_record.mvcc.xmin = tx_id;
    mvcc_record.mvcc.xmax = 0;

    strncpy(mvcc_record.value, record, sizeof(mvcc_record.value) - 1);
    mvcc_record.value[sizeof(mvcc_record.value) - 1] = '\0';

    memcpy(page->data + offset, &mvcc_record, sizeof(MVCCRecord));

    page->num_records++;

    return 0;
}

/* Nuevo SELECT con visibilidad MVCC */
void page_print_mvcc_records(const Page *page, const MVCCSnapshot *snapshot) {
    if (!page || !snapshot) return;

    int visible_count = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (mvcc_is_visible(record.mvcc, snapshot)) {
            printf("  [%d] %s  (xmin=%d, xmax=%d)\n",
                   i + 1,
                   record.value,
                   record.mvcc.xmin,
                   record.mvcc.xmax);

            visible_count++;
        }
    }

    if (visible_count == 0) {
        printf("  Sin registros visibles para esta transacción.\n");
    }
}

/* DELETE lógico con MVCC: no borra físicamente, solo marca xmax */
int page_mark_delete_mvcc_record(Page *page, const char *record, int tx_id, const MVCCSnapshot *snapshot) {
    if (!page || !record || !snapshot) return -1;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord current;

        memcpy(&current,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (strcmp(current.value, record) == 0 &&
            mvcc_is_visible(current.mvcc, snapshot)) {

            current.mvcc.xmax = tx_id;

            memcpy(page->data + (i * (int)sizeof(MVCCRecord)),
                   &current,
                   sizeof(MVCCRecord));

            return 0;
        }
    }

    return -1;
}