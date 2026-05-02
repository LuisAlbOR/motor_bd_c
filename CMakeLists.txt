cmake_minimum_required(VERSION 3.10)
project(motor_db C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")

set(SOURCES
    main.c
    storage/page.c
    storage/file_manager.c
    buffer/buffer_pool.c
    index/btree.c
    transaction/wal.c
    transaction/transaction.c
    transaction/recovery.c
    concurrency/lock_manager.c
    query/parser.c
    query/executor.c
    catalog/catalog.c
    nl/nl2sql.c
    network/server.c
    utils/logger.c
)

add_executable(motor_db ${SOURCES})
target_link_libraries(motor_db pthread)
