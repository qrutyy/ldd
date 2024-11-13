#include <linux/hashtable.h>
#include <linux/btree.h>
#include "dscontrol.h"
#include "btreeutils.h"
#include "hashtableutils.h"
#include "skiplist.h"

int ds_init(struct data_struct *ds, char* sel_ds)
{
	struct btree *btree_map = NULL;
	struct btree_head *root = NULL;
	struct hashtable *hash_table = NULL; 
	struct hash_el *last_hel = NULL;
	int status = 0;
	char* bt = "bt";
	char* sl = "sl";
	char* hm = "hm";

	if (!strncmp(sel_ds, bt, 2)) {
		btree_map = kzalloc(sizeof(struct btree), GFP_KERNEL);
		if (!btree_map)
			goto mem_err;

		root = kzalloc(sizeof(struct btree_head), GFP_KERNEL);
		if (!root)
			goto mem_err;
			
		status = btree_init(root);
		if (status) 
			return status;

		btree_map->head = root;
		ds->type = BTREE_TYPE;
		ds->structure.map_tree = btree_map;
	}
	if (!strncmp(sel_ds, sl, 2)) {
		struct skiplist *sl_map = skiplist_init();
		ds->type = SKIPLIST_TYPE;
		ds->structure.map_list = sl_map;
	}
	if (!strncmp(sel_ds, hm, 2)) {
		hash_table = kzalloc(sizeof(struct hashtable), GFP_KERNEL);
		last_hel = kzalloc(sizeof(struct hash_el), GFP_KERNEL);
		hash_table->last_el = last_hel;
		if (!hash_table)
			goto mem_err;

		hash_init(hash_table->head);
		ds->type = HASHTABLE_TYPE;
		ds->structure.map_hash = hash_table;  
	}
	return 0;
	
mem_err:
	pr_err("Memory allocation failed\n");
	kfree(ds);
	kfree(root);
	kfree(hash_table);
	kfree(last_hel);
	return -ENOMEM;
}

void ds_free(struct data_struct *ds) {
	if (ds->type == BTREE_TYPE) {
		btree_destroy(ds->structure.map_tree->head);
		ds->structure.map_tree = NULL;
	}
	if (ds->type == SKIPLIST_TYPE) {
		skiplist_free(ds->structure.map_list);
		ds->structure.map_list = NULL;
	}
	if (ds->type == HASHTABLE_TYPE) {
		hashtable_free(ds->structure.map_hash);
		ds->structure.map_hash = NULL;
	}
}

void* ds_lookup(struct data_struct *ds, sector_t *key)
{
	struct skiplist_node *sl_node = NULL;
	struct hash_el *hm_node = NULL;

	if (ds->type == BTREE_TYPE) {
		return btree_lookup(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		sl_node = skiplist_find_node(ds->structure.map_list, *key);
		if (sl_node == NULL)
			return NULL;
		if (sl_node->data == NULL) {
			pr_info("Warning: Data in skiplist node is NULL\n");
			return NULL;
		}
		return sl_node->data;
	}
	if (ds->type == HASHTABLE_TYPE) {
		hm_node = hashtable_find_node(ds->structure.map_hash, *key);
		if (hm_node == NULL || hm_node->value)
			return NULL;

		return hm_node->value;
	}
	return NULL;
}

void ds_remove(struct data_struct *ds, sector_t *key)
{
	struct hlist_node hm_node;

	if (ds->type == BTREE_TYPE) {
		btree_remove(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		skiplist_remove(ds->structure.map_list, *key);
	}
	if (ds->type == HASHTABLE_TYPE) {
		hm_node = hashtable_find_node(ds->structure.map_hash, *key)->node;	
		hash_del(&hm_node);
	}
}

int ds_insert(struct data_struct *ds, sector_t *key, void* value)
{
	struct hash_el *el;
	if (ds->type == BTREE_TYPE) {
		return btree_insert(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key, value, GFP_KERNEL);
	}
	if (ds->type == SKIPLIST_TYPE) {
		skiplist_add(ds->structure.map_list, *key, value);
	}
	if (ds->type == HASHTABLE_TYPE) {
		el = kzalloc(sizeof(struct hash_el), GFP_KERNEL);
		if (!el)
			goto mem_err;

		el->key = *key;
		el->value = value;
		hash_add_cs(ds->structure.map_hash->head, &el->node, *key);
		if (ds->structure.map_hash->last_el->key < *key) {
			ds->structure.map_hash->last_el = el;
		}
	}
	return 0;

mem_err:
	pr_err("Memory allocation failed\n");
	kfree(el);
	return -ENOMEM;
}

void* ds_last(struct data_struct *ds, sector_t *key)
{
	struct hash_el *hm_node;
	struct skiplist_node *sl_node;
	if (ds->type == BTREE_TYPE) {
		return btree_last_no_rep(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		sl_node = skiplist_last(ds->structure.map_list);
		if (!sl_node)
			return NULL;
		return sl_node->data;
	}
	if (ds->type == HASHTABLE_TYPE) {
		hm_node = ds->structure.map_hash->last_el;
		if (hm_node && hm_node->value)
			return hm_node->value;
	}
	return NULL;
}

void* ds_prev(struct data_struct *ds, sector_t *key)
{
	struct hash_el *hm_node;
	if (ds->type == BTREE_TYPE) {
		return btree_get_prev_no_rep(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_prev(ds->structure.map_list, *key)->data;
	}
	if (ds->type == HASHTABLE_TYPE) {
		hm_node = hashtable_prev(ds->structure.map_hash, *key);
		if (!hm_node) {
			pr_info("Prev key hasn't been found in his/prev bucket\n");
			return NULL;
		}
		return hm_node->value;
	}
	return NULL;
}

int ds_empty_check(struct data_struct *ds)
{
	if (ds->type == BTREE_TYPE && ds->structure.map_tree->head->height == 0)
		return 1;
	if (ds->type == SKIPLIST_TYPE && ds->structure.map_list->head_lvl == 0) 
		return 1;
	if (ds->type == HASHTABLE_TYPE && hash_empty(ds->structure.map_hash->head))
		return 1;
	return 0;
}

