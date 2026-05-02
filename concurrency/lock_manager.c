#include "lock_manager.h"
#include <pthread.h>

static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

void lock_manager_init(void) {}
void lock_database(void) { pthread_mutex_lock(&db_lock); }
void unlock_database(void) { pthread_mutex_unlock(&db_lock); }
