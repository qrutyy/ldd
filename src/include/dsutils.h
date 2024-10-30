/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <linux/types.h>

enum data_type {
	BTREE_TYPE,
    SKIPLIST_TYPE
};

struct data_struct {
	enum data_type type;
	union {
        struct btree *map_tree;
        struct skiplist *map_list;
    } structure;
};

int ds_init(struct data_struct *ds, char* sel_ds);
void ds_free(struct data_struct *ds);
void* ds_lookup(struct data_struct *ds, sector_t *key);
void ds_remove(struct data_struct *ds, sector_t *key);
int ds_insert(struct data_struct *ds, sector_t *key, void* value);
void* ds_last(struct data_struct *ds, sector_t *key);
void* ds_prev(struct data_struct *ds, sector_t *key);
int ds_empty_check(struct data_struct *ds);

