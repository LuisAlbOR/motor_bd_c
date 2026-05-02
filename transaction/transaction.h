#ifndef TRANSACTION_H
#define TRANSACTION_H

typedef enum {
    TX_NONE,
    TX_ACTIVE,
    TX_COMMITTED,
    TX_ABORTED
} TransactionState;

typedef struct {
    int transaction_id;
    TransactionState state;
} Transaction;

void tx_init(Transaction *tx);
void tx_begin(Transaction *tx);
void tx_commit(Transaction *tx);
void tx_rollback(Transaction *tx);
int tx_is_active(const Transaction *tx);

#endif
