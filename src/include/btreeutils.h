/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <linux/btree.h>

struct btree {
	struct btree_head *head;
};

void *btree_last_no_rep(struct btree_head *head, struct btree_geo *geo, unsigned long *key);
void *btree_get_next(struct btree_head *head, struct btree_geo *geo, unsigned long *key);
void *btree_get_prev_no_rep(struct btree_head *head, struct btree_geo *geo, unsigned long *key);
