// SPDX-License-Identifier: GPL-2.0-only

#include <linux/hashtable.h>
#include "hashtableutils.h"
#include <linux/slab.h>

void hashmap_free(struct hashmap *hm) {
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm->head, bckt_iter, el, node)
		if (el != NULL) {
			hash_del(&el->node);
			kfree(el);
	}
	// add full free?
}

struct hash_el* hashmap_find_node(struct hashmap *hm, sector_t key) {
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm->head, bckt_iter, el, node)
		if (el != NULL && el->key == key)
			return el;
	return NULL;
}

struct hash_el* hashmap_last(struct hashmap *hm) {
	struct hash_el *max_value_node;
	uint64_t max_key = 0;
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm->head, bckt_iter, el, node)
		if (el->key > max_key) {
			max_key = el->key;
			max_value_node = el;
		} 
	return max_value_node;
}

struct hash_el* hashmap_prev(struct hashmap *hm, sector_t key) {
	struct hash_el *prev_max_node;
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm->head, bckt_iter, el, node)
		if (el->key < key) {
			prev_max_node = el;
		} 
	return prev_max_node;
}


