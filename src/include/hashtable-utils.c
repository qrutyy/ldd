// SPDX-License-Identifier: GPL-2.0-only

#include <linux/hashtable.h>
#include "hashtable-utils.h"
#include <linux/slab.h>


void hash_add_cs(struct hlist_head *hm_head, struct hlist_node *node, sector_t key) {
	hlist_add_head(node, &hm_head[hash_min(BUCKET_NUM, HT_MAP_BITS)]);
}

void hashtable_free(struct hashtable *hm) {
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

struct hash_el* hashtable_find_node(struct hashtable *hm, sector_t key) {
	struct hash_el *el;
	
	pr_debug("Hashtable: bucket_val %llu", BUCKET_NUM);

	hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM, HT_MAP_BITS)], node)
		if (el != NULL && el->key == key)
			return el;

	return NULL;
}

struct hash_el* hashtable_prev(struct hashtable *hm, sector_t key) {
	struct hash_el *prev_max_node = kzalloc(sizeof( struct hash_el), GFP_KERNEL);
	struct hash_el *el;
	
	hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM, HT_MAP_BITS)], node) {
		if (el && el->key <= key && el->key > prev_max_node->key)
			prev_max_node = el;
	}

	if (prev_max_node->key == 0) {
		pr_debug("Hashtable: Element with  is in the prev bucket\n");
		// mb execute rexursively key + mb_size
		hlist_for_each_entry(el, &hm->head[hash_min(BUCKET_NUM + 1, HT_MAP_BITS)], node) {
			if (el && el->key <= key && el->key > prev_max_node->key) {
				prev_max_node = el;
			}
			pr_debug("Hashtable: prev el key = %llu\n", el->key);
		}
		if (prev_max_node->key == 0)
			return NULL;
	}
	pr_debug("Hashtabel: Element with prev key - el key=%llu, val=%p\n", prev_max_node->key, prev_max_node->value);

	return prev_max_node;
}


