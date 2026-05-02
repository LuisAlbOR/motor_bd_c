#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "page.h"

int fm_open_table(const char *table_name);
int fm_read_page(int fd, int page_id, Page *page);
int fm_write_page(int fd, int page_id, const Page *page);
int fm_table_exists(const char *table_name);
int fm_create_table_file(const char *table_name);

#endif
