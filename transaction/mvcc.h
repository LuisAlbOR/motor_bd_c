#ifndef MVCC_H
#define MVCC_H

#define MVCC_MAX_TX 100
#define MVCC_MAX_ACTIVE 100

typedef enum {
    MVCC_TX_NONE = 0,
    MVCC_TX_ACTIVE,
    MVCC_TX_COMMITTED,
    MVCC_TX_ABORTED
} MVCCStatus;

typedef struct {
    int tx_id;
    MVCCStatus status;
} MVCCTxEntry;

typedef struct {
    int xmin;
    int xmax;
} MVCCHeader;

typedef struct {
    int tx_id;
    int xmin;
    int xmax;
    int active_txs[MVCC_MAX_ACTIVE];
    int active_count;
} MVCCSnapshot;

void mvcc_init(void);

int mvcc_begin(void);
void mvcc_commit(int tx_id);
void mvcc_abort(int tx_id);

MVCCSnapshot mvcc_create_snapshot(int tx_id);

int mvcc_is_active(int tx_id);
int mvcc_is_committed(int tx_id);
int mvcc_is_aborted(int tx_id);

int mvcc_snapshot_contains(const MVCCSnapshot *snapshot, int tx_id);
int mvcc_is_visible(MVCCHeader header, const MVCCSnapshot *snapshot);

#endif