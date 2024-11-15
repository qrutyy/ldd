/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Originail author: Egor Shalashnov @egshnov
 * 
 * Modified by Mikhail Gavrilenko on 14.11.24 
 * Changes: rename function, add get_last and get_prev methods
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include "rbtree.h"

static struct rbtree_node *create_rbtree_node(sector_t key, void* value)
{
	struct rbtree_node *node;

	node = kzalloc(sizeof(struct rbtree_node ), GFP_KERNEL);
	if (!node)
		return NULL;
	node->key = key;
	node->value = value;

	return node;
}

static void free_rbtree_node(struct rbtree_node *node)
{
	kfree(node->value);
	kfree(node);
}

static int compare_keys(sector_t lkey, sector_t rkey)
{
	if (!(lkey && rkey))
		return -5;
	return lkey < rkey ? -1 : (lkey == rkey ? 0 : 1);
}

static struct rbtree_node *__rbtree_underlying_search(struct rb_root *root,
							 sector_t key)
{
	struct rb_node *node;
	node = root->rb_node;
	pr_info("1\n");
	while (node) {
		struct rbtree_node *data =
			container_of(node, struct rbtree_node, node);
		pr_info("%llu \n ", data->key);
		int result = compare_keys(key, data->key);
		pr_info("2\n");
		if (result == -5)
			return NULL;
		pr_info("3\n");
		pr_info("result = %d rb_right %p rb_left %p \n", result, node->rb_right, node->rb_left);
		pr_info("root = %p\n", root);
		if (result < 0 && node->rb_left) {
			node = node->rb_left;
			pr_info("4\n");
		}

		else if (result > 0 && node->rb_right) {
			node = node->rb_right;
			pr_info("5\n");
		}
		else
			return data;
	}
	pr_info("10\n");
	return NULL;
}

static int __rbtree_underlying_insert(struct rb_root *root, sector_t key, void* value)
{
	bool overwrite;
	struct rb_node **new;
	struct rb_node *parent;
	struct rbtree_node *data;
	struct rbtree_node *this;
	int result;

	overwrite = 0;
	new = &(root->rb_node);
	parent = NULL;

	while (*new) {
		this = container_of(*new, struct rbtree_node, node);

		result = compare_keys(key, this->key);
		parent = *new;

		if (result < 0) {
			new = &((*new)->rb_left);
		} else if (result > 0) {
			new = &((*new)->rb_right);
		} else {
			overwrite = true;
			this->value = value;
			return 0;
		}
	}

	if (!overwrite) {
		data = create_rbtree_node(key, value);
		if (!data)
			goto no_mem;
		rb_link_node(&data->node, parent, new);
		rb_insert_color(&data->node, root);
	}
	return sizeof(struct rbtree_node);

no_mem:
	return -ENOMEM;
}

struct rbtree *rbtree_init(void)
{
	struct rbtree *new_tree;

	new_tree = kzalloc(sizeof(struct rbtree), GFP_KERNEL);
	if (!new_tree)
		return NULL;

	new_tree->root = RB_ROOT;
	new_tree->node_num = 0;
	return new_tree;
}

void rbtree_free(struct rbtree *rbt)
{
	if (!rbt)
		return;

	struct rbtree_node *pos, *node;
	rbtree_postorder_for_each_entry_safe(pos, node, &(rbt->root), node)	  {
		free_rbtree_node(pos);
	}
	kfree(rbt);
}

void rbtree_remove(struct rbtree *rbt, sector_t key)
{
	struct rbtree_node *data;

	data = __rbtree_underlying_search(&(rbt->root), key);
	if (data) {
		rb_erase(&(data->node), &(rbt->root));
		free_rbtree_node(data);
	}
	rbt->node_num--;
}

void rbtree_add(struct rbtree *rbt, sector_t key, void* value)
{
	__rbtree_underlying_insert(&(rbt->root), key, value);
	rbt->node_num++;
}

struct rbtree_node *rbtree_find_node(struct rbtree *rbt, sector_t key)
{
	struct rbtree_node *target;
	target = __rbtree_underlying_search(&(rbt->root), key);
	return target;
}

struct rbtree_node *rbtree_last(struct rbtree *rbt) {
	struct rb_root root = rbt->root;
    struct rb_node *node = root.rb_node;

	if (!node)
        return NULL; 
	if (!(node->rb_left && node->rb_right))
		return NULL;
    
	struct rbtree_node *data =
			container_of(node, struct rbtree_node, node);
	if (!(data->key && data->value)) {
		pr_debug("key = %llu\n", data->key);
		return NULL;
	}
	
	while (node) {
		if (node->rb_right) {
	        node = node->rb_right;
		}
		else
			break;
	}
    return container_of(node, struct rbtree_node, node);
}

struct rbtree_node *rbtree_prev(struct rbtree *rbt, sector_t key) {
    struct rbtree_node *curr;
	struct rb_root root = rbt->root;
	curr = __rbtree_underlying_search(&root, key);
    if (!curr)
        return NULL;  

    struct rb_node *node = &curr->node;
    
    if (node) {
        node = node->rb_left;
        while (node->rb_right)
            node = node->rb_right;
        return container_of(node, struct rbtree_node, node);
    }

    struct rb_node *ancestor = root.rb_node;
    struct rbtree_node *prev = NULL;

    while (ancestor) {
        struct rbtree_node *ancestor_data = container_of(ancestor, struct rbtree_node, node);

        if (compare_keys(ancestor_data->key, key) < 0) {
            prev = ancestor_data;
            ancestor = ancestor->rb_right;
        } else {
            ancestor = ancestor->rb_left;
        }
    }

    return prev;
}
