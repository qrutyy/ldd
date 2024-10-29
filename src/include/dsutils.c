
#include <ios>
int ds_init(struct data_struct curr_ds)
{
	if (strcmp(sel_ds, "bt")) {
		struct btree *ds = kmalloc(sizeof(struct btree), GFP_KERNEL);
		if (!ds)
			goto mem_err;

		struct btree_head *root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);
		if (!root)
			goto mem_err;
		
		btree_init(root);
		curr_ds.type = BTREE_TYPE;
		curr_ds.structure = ds;
		curr_ds.structure->head = root;
	}
	if (strcmp(sel_ds, "sl")) {
		struct skiplist *ds = skiplist_init();
		curr_ds.type = SKIPLIST_TYPE;
		curr_ds.structure = ds;
	}

	return 0;
	
mem_err:
	pr_err("Memory allocation faile\n");
	kfree(ds);
	kfree(root);
	return -ENOMEM;
}

void ds_free(struct data_struct curr_ds) {
	if (curr_ds.type == BTREE_TYPE)
		btree_destroy(curr_ds.structure->head);
	if (curr_ds.type == SKIPLIST_TYPE)
		skiplist_free(curr_ds.structure);
}

int ds_lookup(struct data_struct curr_ds, bdrm_sector key)
{
	if (curr_ds.type == BTREE_TYPE) {
		return btree_lookup(curr_ds.structure.map_tree->head, &btree_geo64, key);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		return skiplist_find_node(key, curr_ds.structure.map_list)->data;
	}
	return 0;
}

int ds_remove(struct data_struct curr_ds, bdrm_sector key)
{
	int status;
	if (curr_ds.type == BTREE_TYPE) {
		status = btree_remove(curr_ds.structure.map_tree->head, &btree_geo64, key);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		status = skiplist_remove(key, curr_ds.structure.map_list);
	}
	if (!status)
		pr_info("ERROR: Failed to remove %d from _\n", sector); // TODO: add ds name
	return status;
}

int ds_insert(struct data_struct curr_ds, bdrm_sector key, struct *redir_sector_info value)
{
	if (curr_ds.type == BTREE_TYPE) {
		return btree_insert(curr_ds.structure.map_tree->head, &btree_geo64, key, value, GFP_KERNEL);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		return skiplist_add(key, value, curr_ds.structure.map_list)->data;
	}
	return 0;
}

void* get_curr_ds_head(struct bdrm_manager *current_bdev_manager)
{
	if (current_bdev_manager->sel_data_struct->type == BTREE_TYPE) 
		return current_bdev_manager->sel_data_struct->structure.map_tree->head;
	if (current_bdev_manager->sel_data_struct->type == SKIPLIST_TYPE)
		return current_bdev_manager->sel_data_struct->structure.map_list->head;
	return NULL;
}

