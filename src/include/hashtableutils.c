// SPDX-License-Identifier: GPL-2.0-only

#include <linux/hashtable.h>
#include "hashtableutils.h"
#include <linux/slab.h>


void hash_add_cs(struct hlist_head *hm_head, struct hlist_node *node, sector_t key) {
	hlist_add_head(node, &hm_head[hash_min(BUCKET_NUM, HT_MAP_BITS)]);
}

void hashmap_free(struct hashmap *hm) {
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm->head, bckt_iter, el, node)
		if (el != NULL) {
			hash_del(&el->node);
			kfree(el);
	}
	kfree(hm->last_el);
	kfree(hm);
}

struct hash_el* hashmap_find_node(struct hashmap *hm, sector_t key) {
	struct hash_el *el;
	
	pr_info("bucket_val %llu", BUCKET_NUM);

	hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM, HT_MAP_BITS)], node)
		if (el != NULL && el->key == key) {
			pr_info("el key = %llu\n", el->key);
			return el;
		} 
	return NULL;
}

struct hash_el* hashmap_prev(struct hashmap *hm, sector_t key) {
	struct hash_el *prev_max_node = kzalloc(sizeof( struct hash_el), GFP_KERNEL);
	struct hash_el *el;
	
	pr_info("bucket_val %llu", BUCKET_NUM);

	hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM, HT_MAP_BITS)], node) {
		if (el && el->key <= key && el->key > prev_max_node->key) {
			prev_max_node = el;
		}
		pr_info("prev el key = %llu\n", el->key);
	}
	if (prev_max_node->key == 0) {
		pr_info("Prev is in the prev bucket\n");
		// mb execute rexursively key + mb_size
		hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM + 1, HT_MAP_BITS)], node) {
			if (el && el->key <= key && el->key > prev_max_node->key) {
				prev_max_node = el;
			}
			pr_info("prev el key = %llu\n", el->key);
		}
		if (prev_max_node->key == 0)
			return NULL;
	}
	pr_info("el key = %llu, val %p, p %p\n", prev_max_node->key, prev_max_node->value, prev_max_node);

	return prev_max_node;
}


