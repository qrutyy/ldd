/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Originail author: Daniel Vlasenco @spisladqo
 * 
 * Modified by Mikhail Gavrilenko on (30.10.24 - last_change) 
 * Changes: 
 * - add skiplist_prev, skiplist_last
 * - edit input types
 */

#include <linux/module.h>

struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	sector_t key;
	void* data;
};

struct skiplist {
	struct skiplist_node *head;
	int head_lvl;
	int max_lvl;
};

struct skiplist *skiplist_init(void);
struct skiplist_node *skiplist_find_node(struct skiplist *sl, sector_t key);
void skiplist_free(struct skiplist *sl);
void skiplist_print(struct skiplist *sl);
struct skiplist_node *skiplist_add(struct skiplist *sl, sector_t key, void* data);
struct skiplist_node *skiplist_remove(struct skiplist *sl, sector_t key);
struct skiplist_node *skiplist_prev(struct skiplist *sl, sector_t key);
struct skiplist_node *skiplist_last(struct skiplist *sl);
