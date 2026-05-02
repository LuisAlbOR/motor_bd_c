CC=gcc
CFLAGS=-D_GNU_SOURCE -std=c11 -Wall -Wextra
LIBS=-lpthread

SOURCES=main.c \
 storage/page.c storage/file_manager.c \
 buffer/buffer_pool.c \
 index/btree.c \
 transaction/wal.c transaction/transaction.c transaction/recovery.c transaction/mvcc.c \
 concurrency/lock_manager.c \
 query/parser.c query/executor.c \
 catalog/catalog.c \
 nl/nl2sql.c \
 network/server.c \
 utils/logger.c

all: motor_db client_db

motor_db:
	$(CC) $(CFLAGS) $(SOURCES) -o motor_db $(LIBS)

client_db:
	$(CC) $(CFLAGS) network/client.c -o client_db

run: motor_db
	./motor_db

server: motor_db
	./motor_db --server 5000

client: client_db
	./client_db 127.0.0.1 5000

clean:
	rm -f motor_db client_db
	rm -rf data/*.tbl data/catalog.dat logs/*.log

read_table:
	$(CC) $(CFLAGS) tools/read_table.c storage/page.c transaction/mvcc.c -o read_table $(LIBS)