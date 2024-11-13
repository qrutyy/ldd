// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#define MAX_BD_NAME_LENGTH 15
#define MAX_MINORS_AM 20
#define MAX_DS_NAME_LEN 2
#define MAIN_BLKDEV_NAME "lsvbd"
#define POOL_SIZE 50
#define SECTOR_OFFSET 32

struct redir_sector_info {
	sector_t *redirected_sector;
	unsigned int block_size;
};

struct bdd_manager {
	char *bd_name;
	struct gendisk *middle_disk;
	struct bdev_handle *bdev_handler;
	struct data_struct *sel_data_struct;
	struct list_head list;
};
