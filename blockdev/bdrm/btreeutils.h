// SPDX-License-Identifier: GPL-2.0-only

#ifndef BTREEUTILS_H
#define BTREEUTILS_H

#include <linux/btree.h>

void *btree_last_no_rep(struct btree_head *head, struct btree_geo *geo, unsigned long *key);
void *btree_get_next(struct btree_head *head, struct btree_geo *geo, unsigned long *key);
void *btree_get_prev_no_rep(struct btree_head *head, struct btree_geo *geo, unsigned long *key);

#endif /* BTREE_HELPERS_H */