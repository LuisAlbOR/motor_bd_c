#include "transaction.h"
#include "wal.h"
#include <stdio.h>
#include <string.h>

static int next_tx_id = 1;

void tx_init(Transaction *tx) {
    tx->transaction_id = 0;
    tx->state = TX_NONE;
}

void tx_begin(Transaction *tx) {
    tx->transaction_id = next_tx_id++;
    tx->state = TX_ACTIVE;
    FILE *log = wal_open();
    LogRecord r = {tx->transaction_id, LOG_BEGIN, "", -1, "", ""};
    wal_write(log, r);
    wal_close(log);
    printf("Transacción %d iniciada.\n", tx->transaction_id);
}

void tx_commit(Transaction *tx) {
    if (!tx || tx->state != TX_ACTIVE) {
        printf("No hay transacción activa.\n");
        return;
    }
    FILE *log = wal_open();
    LogRecord r = {tx->transaction_id, LOG_COMMIT, "", -1, "", ""};
    wal_write(log, r);
    wal_close(log);
    tx->state = TX_COMMITTED;
    printf("Transacción %d confirmada.\n", tx->transaction_id);
}

void tx_rollback(Transaction *tx) {
    if (!tx || tx->state != TX_ACTIVE) {
        printf("No hay transacción activa.\n");
        return;
    }
    FILE *log = wal_open();
    LogRecord r = {tx->transaction_id, LOG_ABORT, "", -1, "", ""};
    wal_write(log, r);
    wal_close(log);
    tx->state = TX_ABORTED;
    printf("Transacción %d cancelada. Nota: este prototipo evita persistir cambios hasta COMMIT.\n", tx->transaction_id);
}

int tx_is_active(const Transaction *tx) {
    return tx && tx->state == TX_ACTIVE;
}
