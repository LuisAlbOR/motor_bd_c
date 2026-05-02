#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "../buffer/buffer_pool.h"
#include "../transaction/transaction.h"

void executor_init(BufferPool *bp, Transaction *tx);
void execute_sql(const char *query);

/* Para que el servidor pueda devolver resultados al cliente */
void executor_set_output_buffer(char *buffer, int size);
void executor_clear_output_buffer(void);
void executor_append_output(const char *text);



const char *executor_get_current_database(void);


#endif