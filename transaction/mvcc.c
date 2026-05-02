#include "mvcc.h"
#include <string.h>
#include <pthread.h>

static MVCCTxEntry tx_table[MVCC_MAX_TX];
static int next_mvcc_tx_id = 1;
static pthread_mutex_t mvcc_mutex = PTHREAD_MUTEX_INITIALIZER;

void mvcc_init(void) {
    pthread_mutex_lock(&mvcc_mutex);

    memset(tx_table, 0, sizeof(tx_table));
    next_mvcc_tx_id = 1;

    pthread_mutex_unlock(&mvcc_mutex);
}

int mvcc_begin(void) {
    pthread_mutex_lock(&mvcc_mutex);

    int tx_id = next_mvcc_tx_id++;

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == 0) {
            tx_table[i].tx_id = tx_id;
            tx_table[i].status = MVCC_TX_ACTIVE;
            break;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);

    return tx_id;
}

void mvcc_commit(int tx_id) {
    pthread_mutex_lock(&mvcc_mutex);

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == tx_id) {
            tx_table[i].status = MVCC_TX_COMMITTED;
            break;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);
}

void mvcc_abort(int tx_id) {
    pthread_mutex_lock(&mvcc_mutex);

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == tx_id) {
            tx_table[i].status = MVCC_TX_ABORTED;
            break;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);
}

int mvcc_is_active(int tx_id) {
    pthread_mutex_lock(&mvcc_mutex);

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == tx_id) {
            int result = tx_table[i].status == MVCC_TX_ACTIVE;
            pthread_mutex_unlock(&mvcc_mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);
    return 0;
}

int mvcc_is_committed(int tx_id) {
    if (tx_id == 0) return 1;

    pthread_mutex_lock(&mvcc_mutex);

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == tx_id) {
            int result = tx_table[i].status == MVCC_TX_COMMITTED;
            pthread_mutex_unlock(&mvcc_mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);

    /*
      Si la transacción no está en memoria, asumimos que viene de disco
      y que fue confirmada antes de reiniciar el motor.
    */
    return 1;
}

int mvcc_is_aborted(int tx_id) {
    pthread_mutex_lock(&mvcc_mutex);

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id == tx_id) {
            int result = tx_table[i].status == MVCC_TX_ABORTED;
            pthread_mutex_unlock(&mvcc_mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);
    return 0;
}

MVCCSnapshot mvcc_create_snapshot(int tx_id) {
    MVCCSnapshot snapshot;
    memset(&snapshot, 0, sizeof(MVCCSnapshot));

    pthread_mutex_lock(&mvcc_mutex);

    snapshot.tx_id = tx_id;
    snapshot.xmin = next_mvcc_tx_id;
    snapshot.xmax = next_mvcc_tx_id;

    for (int i = 0; i < MVCC_MAX_TX; i++) {
        if (tx_table[i].tx_id != 0 &&
            tx_table[i].status == MVCC_TX_ACTIVE &&
            tx_table[i].tx_id != tx_id) {

            if (snapshot.active_count < MVCC_MAX_ACTIVE) {
                snapshot.active_txs[snapshot.active_count++] = tx_table[i].tx_id;
            }

            if (tx_table[i].tx_id < snapshot.xmin) {
                snapshot.xmin = tx_table[i].tx_id;
            }
        }
    }

    pthread_mutex_unlock(&mvcc_mutex);

    return snapshot;
}

int mvcc_snapshot_contains(const MVCCSnapshot *snapshot, int tx_id) {
    if (!snapshot) return 0;

    for (int i = 0; i < snapshot->active_count; i++) {
        if (snapshot->active_txs[i] == tx_id) {
            return 1;
        }
    }

    return 0;
}

int mvcc_is_visible(MVCCHeader header, const MVCCSnapshot *snapshot) {
    if (!snapshot) return 0;

    if (header.xmin == snapshot->tx_id) {
        return header.xmax == 0 || header.xmax == snapshot->tx_id;
    }

    if (!mvcc_is_committed(header.xmin)) {
        return 0;
    }

    if (mvcc_snapshot_contains(snapshot, header.xmin)) {
        return 0;
    }

    if (header.xmax == 0) {
        return 1;
    }

    if (header.xmax == snapshot->tx_id) {
        return 0;
    }

    if (!mvcc_is_committed(header.xmax)) {
        return 1;
    }

    if (mvcc_snapshot_contains(snapshot, header.xmax)) {
        return 1;
    }

    return 0;
}