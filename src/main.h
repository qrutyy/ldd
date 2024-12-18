/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#define LSBDD_MAX_BD_NAME_LENGTH 15
#define LSBDD_MAX_MINORS_AM 20
#define LSBDD_MAX_DS_NAME_LEN 2
#define LSBDD_BLKDEV_NAME_PREFIX "lsvbd"
#define LSBDD_SECTOR_OFFSET 32

struct redir_sector_info {
	sector_t redirected_sector;
	u32 block_size;
};

struct bd_manager {
	char *vbd_name;
	struct gendisk *vbd_disk;
	struct bdev_handle *bd_handler;
	struct data_struct *sel_data_struct;
	struct list_head list;
};

struct sectors {
	sector_t original;
	sector_t redirect;
};
