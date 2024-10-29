/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Originail author: Daniel Vlasenco @spisladqo
 * 
 * Modified by Mikhail Gavrilenko on _ 
 * Changes: _
 * (REMOVE IF NOT MODIFIED)
 */

#include <linux/module.h>

struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	sector_t key;
	sector_t data;
};

struct skiplist {
	struct skiplist_node *head;
	int head_lvl;
	int max_lvl;
};

struct skiplist *skiplist_init(void);
struct skiplist_node *skiplist_find_node(sector_t key, struct skiplist *sl);
void skiplist_free(struct skiplist *sl);
void skiplist_print(struct skiplist *sl);
struct skiplist_node *skiplist_add(sector_t key, sector_t data,
					struct skiplist *sl);
