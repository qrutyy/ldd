#include <linux/btree.h>
#include "dsutils.h"
#include "btreeutils.h"
#include "skiplist.h"
#include "../main.h"

int ds_init(struct data_struct *ds, char* sel_ds)
{
	struct btree *btree_map;
	struct btree_head *root;
	int status = 0;

	if (strcmp(sel_ds, "bt")) {
		btree_map = kmalloc(sizeof(struct btree), GFP_KERNEL);
		if (!ds)
			goto mem_err;

		root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);
		if (!root)
			goto mem_err;
		
		status = btree_init(root);
		if (status) 
			return status;
		ds->type = BTREE_TYPE;
		ds->structure.map_tree = btree_map;
		ds->structure.map_list = NULL;
		ds->structure.map_tree->head = root;
	}
	if (strcmp(sel_ds, "sl")) {
		struct skiplist *sl_map = skiplist_init();
		ds->type = SKIPLIST_TYPE;
		ds->structure.map_list = sl_map;
		ds->structure.map_tree = NULL;
	}

	return 0;
	
mem_err:
	pr_err("Memory allocation faile\n");
	kfree(ds);
	kfree(root);
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
}

void* ds_lookup(struct data_struct *ds, sector_t *key)
{
	if (ds->type == BTREE_TYPE) {
		return btree_lookup(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_find_node(*key, ds->structure.map_list)->data;
	}
	return 0;
}

void ds_remove(struct data_struct *ds, sector_t *key)
{
	if (ds->type == BTREE_TYPE) {
		btree_remove(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		skiplist_remove(*key, ds->structure.map_list);
	}
}

int ds_insert(struct data_struct *ds, sector_t *key, void* value)
{
	if (ds->type == BTREE_TYPE) {
		return btree_insert(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key, value, GFP_KERNEL);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_add(*key, value, ds->structure.map_list)->data;
	}
	return 0;
}

void* ds_last(struct data_struct *ds, sector_t *key)
{
	if (ds->type == BTREE_TYPE) {
		return btree_last_no_rep(ds->structure.map_tree->head, &btree_geo64, (unsigned long *)key);
	}
	if (ds->type == SKIPLIST_TYPE) {
		return skiplist_last(ds->structure.map_list)->data;
	}
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
	return NULL;
}

int ds_empty_check(struct data_struct *ds)
{
	if (ds->type == BTREE_TYPE && ds->structure.map_tree->head->height == 0)
		return 1;
	if (ds->type == SKIPLIST_TYPE && ds->structure.map_list->head->next == NULL && ds->structure.map_list->head->lower == NULL) 
		return 1;
	return 0;
}

