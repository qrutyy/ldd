#include <linux/hashmap.h>
#include <linux/btree.h>
#include "dsutils.h"
#include "btreeutils.h"
#include "hashmaputils.h"
#include "skiplist.h"
#include "../main.h"

int ds_init(struct data_struct *ds, char* sel_ds)
{
	struct btree *btree_map;
	struct btree_head *root;
	struct hashmap *hash_map; 
	int status = 0;
	char* bt = "bt";
	char* sl = "sl";

	if (!strncmp(sel_ds, bt, 2)) {
		btree_map = kmalloc(sizeof(struct btree), GFP_KERNEL);
		if (!btree_map)
			goto mem_err;

		root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);
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
		hash_map = kmalloc(sizeof(struct hashmap), GFP_KERNEL);
		if (!hash_map)
			goto mem_err;
		hash_init(hash_map->head, HT_MAP_BITS);
		ds->type = HASHMAP_TYPE;
		ds->structure.map_hash = hash_map;  
	}
	return 0;
	
mem_err:
	pr_err("Memory allocation faile\n");
	kfree(ds);
	kfree(root);
	kfree(hash_map);
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
	if (ds->type == HASHMAP_TYPE) {
		hashmap_free(ds->structure.map_hash);
		ds->structure.map_hash = NULL;
	}
}

void* ds_lookup(struct data_struct *ds, sector_t *key)
{
	struct skiplist_node *found_node;

	if (ds->type == BTREE_TYPE) {
		return btree_lookup(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		found_node = skiplist_find_node(ds->structure.map_list, *key);
		if (found_node == NULL)
			return NULL;
		if (found_node->data == NULL) {
			pr_info("Warning: Data in skiplist node is NULL\n");
			return NULL;
		}
		return found_node;
	}
	if (ds->type == HASHMAP_TYPE) {
		return hashmap_find_node(ds->structure.map_hash, *key)->value;
	}
	return NULL;
}

void ds_remove(struct data_struct *ds, sector_t *key)
{
	struct hlist *hm_node;

	if (ds->type == BTREE_TYPE) {
		btree_remove(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		skiplist_remove(ds->structure.map_list, *key);
	}
	if (ds->type == HASHMAP_TYPE) {
		hm_node = hashmap_find_node(ds->structure.map_hash, *key)->node;	
		hash_del(hm_node);
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
	if (ds->type == HASHMAP_TYPE) {
		el = kmalloc(sizeof(struct hash_el), GFP_KERNEL);
		if (!el)
			goto mem_err:

		el->key = *key;
		el->value = value;
		hash_add(ds->structure.map_hash->head, el->node, el);
	}
	return 0;
mem_err:
	pr_err("Memory allocation failed\n");
	kfree(el)
	return -ENOMEM;
}

void* ds_last(struct data_struct *ds, sector_t *key)
{
	pr_info("Last entered\n");
	if (ds->type == BTREE_TYPE) {
		return btree_last_no_rep(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_last(ds->structure.map_list)->data;
	}
	if (ds->type == HASHMAP_TYPE) {
		return hashmap_last(ds->structure.map_hash)->value;
	}
	pr_info("Last exited\n");
	return NULL;
}

void* ds_prev(struct data_struct *ds, sector_t *key)
{
	if (ds->type == BTREE_TYPE) {
		return btree_get_prev_no_rep(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_prev(ds->structure.map_list, *key)->data;
	}
	if (ds->type == HASHMAP_TYPE) {
		return hashmap_prev(ds->structure.map_hash, *key)->value;
	}
	return NULL;
}

int ds_empty_check(struct data_struct *ds)
{
	if (ds->type == BTREE_TYPE && ds->structure.map_tree->head->height == 0)
		return 1;
	if (ds->type == SKIPLIST_TYPE && ds->structure.map_list->head_lvl == 0) 
		return 1;
	if (ds->type == HASHMAP_TYPE && hash_empty(ds->structure.map_hash->head))
		return 1;
	return 0;
}

