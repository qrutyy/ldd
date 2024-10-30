/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Originail author: Daniel Vlasenco @spisladqo
 * 
 * Modified by Mikhail Gavrilenko on _ 
 * Changes: _
 * (REMOVE IF NOT MODIFIED)
 */


#include "skiplist.h"

#define HEAD_KEY ((sector_t)0)
#define HEAD_DATA ((sector_t)U64_MAX)
#define TAIL_KEY ((sector_t)U64_MAX)
#define TAIL_DATA ((sector_t)0)
#define MAX_LVL 20

static void free_node_full(struct skiplist_node *node)
{
	struct skiplist_node *temp;

	while (node) {
		temp = node->lower;
		kfree(node);
		node = temp;
	}
}

static struct skiplist_node *create_node_tall(sector_t key, sector_t data,
								int h)
{
	struct skiplist_node *last;
	struct skiplist_node *curr;
	int curr_h;

	last = NULL;
	for (curr_h = 0; curr_h < h; ++curr_h) {
		curr = kzalloc(sizeof(*curr), GFP_KERNEL);
		if (!curr)
			goto alloc_fail;

		curr->key = key;
		curr->data = data;
		curr->lower = last;
		last = curr;
	}

	return curr;

alloc_fail:
	free_node_full(last);

	return NULL;
}

static struct skiplist_node *create_node(sector_t key, sector_t data)
{
	return create_node_tall(key, data, 1);
}

struct skiplist *skiplist_init(void)
{
	struct skiplist *sl;
	struct skiplist_node *head;
	struct skiplist_node *tail;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	head = create_node(HEAD_KEY, HEAD_DATA);
	tail = create_node(TAIL_KEY, TAIL_DATA);
	if (!sl || !head || !tail)
		goto alloc_fail;

	sl->head = head;
	sl->head_lvl = 0;
	sl->max_lvl = MAX_LVL;
	head->next = tail;

	return sl;

alloc_fail:
	kfree(sl);
	kfree(head);
	kfree(tail);
	return NULL;
}

struct skiplist_node *skiplist_find_node(struct skiplist *sl, sector_t key)
{
	struct skiplist_node *curr = sl->head;

	while (curr) {
		if (curr->next->key == key)
			return curr->next;
		else if (curr->next->key < key)
			curr = curr->next;
		else
			curr = curr->lower;
	}

	return NULL;
}

static int move_head_and_tail_up(struct skiplist *sl, int lvls_up)
{
	struct skiplist_node *head_ext;
	struct skiplist_node *tail_ext;
	struct skiplist_node *curr;
	struct skiplist_node *temp;

	head_ext = create_node_tall(HEAD_KEY, HEAD_DATA, lvls_up);
	tail_ext = create_node_tall(TAIL_KEY, TAIL_DATA, lvls_up);
	if (!head_ext || !tail_ext)
		goto alloc_fail;

	curr = head_ext;
	temp = tail_ext;
	while (curr && temp) {
		curr->next = temp;
		if (!curr->lower || !temp->lower)
			break;

		curr = curr->lower;
		temp = temp->lower;
	}

	curr->lower = sl->head;
	temp->lower = skiplist_find_node(TAIL_KEY, sl);
	sl->head = head_ext;

	return 0;

alloc_fail:
	free_node_full(head_ext);
	free_node_full(tail_ext);
	
	return -ENOMEM;
}

static int move_up_if_lvl_nex(struct skiplist *sl, int lvl)
{
	unsigned int diff;
	int ret;

	if (lvl <= sl->head_lvl || lvl > sl->max_lvl)
		return 0;

	diff = lvl - sl->head_lvl;
	ret = move_head_and_tail_up(sl, diff);
	if (ret) {
		pr_err("failed to move head and tail up\n");
		return ret;
	}
	sl->head_lvl = lvl;

	return 0;
}

static int flip_coin(void)
{
	return get_random_u8() % 2;
}

static int get_random_lvl(int max) {
	int lvl = 0;

	while ((lvl < max) && flip_coin())
		lvl++;

	return lvl;
}

static void get_prev_nodes(sector_t key, struct skiplist *sl,
			struct skiplist_node **buf, int lvl)
{
	struct skiplist_node *curr;
	int curr_lvl;

	curr = sl->head;
	curr_lvl = sl->head_lvl;
	while (curr && curr_lvl >= 0) {
		if (curr->next->key < key) {
			curr = curr->next;
		} else {
			if (curr_lvl <= lvl) {
				buf[curr_lvl] = curr;
			}
			--curr_lvl;
			curr = curr->lower;
		}
	}
}

static struct skiplist_node *skiplist_insert_at_lvl(sector_t key,
		sector_t data, struct skiplist *sl, int lvl)
{
	struct skiplist_node *prev[MAX_LVL+1];
	struct skiplist_node *new;
	struct skiplist_node *temp;
	int i;

	get_prev_nodes(key, sl, prev, lvl);
	temp = NULL;
	for (i = 0; i <= lvl; ++i) {
		new = create_node(key, data);
		if (!new)
			goto fail;
		new->next = prev[i]->next;
		new->lower = temp;
		prev[i]->next = new;
		temp = new;
	}

	return new;
fail:
	for (i = i - 1; i >= 0; --i) {
		new = prev[i]->next;
		prev[i]->next = new->next;
		kfree(new);
	}

	return ERR_PTR(-ENOMEM);
}

struct skiplist_node *skiplist_add(struct skiplist *sl, sector_t key, sector_t data)
{
	struct skiplist_node *old;
	struct skiplist_node *new;
	int lvl;
	int err;

	old = skiplist_find_node(key, sl);
	if (old)
		return old;

	lvl = get_random_lvl(sl->max_lvl);
	err = move_up_if_lvl_nex(sl, lvl);
	if (err)
		goto fail;

	new = skiplist_insert_at_lvl(key, data, sl, lvl);
	if (IS_ERR(new))
		goto fail;

	return new;

fail:
	return ERR_PTR(err);
}


struct skiplist_node *skiplist_remove(struct skiplist *sl, sector_t key) {
    struct skiplist_node *prev[MAX_LVL + 1];
    struct skiplist_node *target;
    struct skiplist_node *temp;
    int lvl;
    int found = 0;

    get_prev_nodes(key, sl, prev, sl->head_lvl);
    target = prev[0]->next;

    if (target && target->key == key) {
        found = 1;
    }

    if (found) {
        for (lvl = 0; lvl <= sl->head_lvl; lvl++) {
            if (prev[lvl]->next == target) {
                prev[lvl]->next = target->next;
            }
        }

        temp = target->lower;
        kfree(target);
        target = temp;

        while (sl->head_lvl > 0 && sl->head->next->key == TAIL_KEY) {
            struct skiplist_node *old_head = sl->head;
            sl->head = sl->head->lower;
            kfree(old_head);
            sl->head_lvl--;
        }
    }

    return found ? target : NULL;
}

void skiplist_free(struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *next;
	struct skiplist_node *tofree;
	struct skiplist_node *tofree_stack[MAX_LVL + 1];
	int stack_i;

	if (!sl)
		return;

	stack_i = 0;
	tofree_stack[stack_i++] = sl->head;

	while (stack_i > 0 && tofree_stack[stack_i]) {
		tofree = tofree_stack[stack_i];

		curr = tofree;
		while (curr) {
			next = curr->next;
			if (!next)
				break;

			if (next->key < tofree_stack[stack_i]->key)
				tofree_stack[++stack_i] = next;

			curr = curr->lower;
		}

		free_node_full(tofree);
		tofree_stack[stack_i--] = NULL;
	}

	kfree(sl);
}

void skiplist_print(struct skiplist *sl) {
	struct skiplist_node *curr;
	struct skiplist_node *head;

	head = sl->head;
	while (head) {
		curr = head;
		while (curr) {
			if (curr->key == HEAD_KEY && curr->data == HEAD_DATA)
				printk(KERN_CONT "head->");
			else if (curr->key == TAIL_KEY && curr->data == TAIL_DATA)
				printk(KERN_CONT "tail->");
			else
				printk(KERN_CONT "(%llu-%llu)->", curr->key, curr->data);

			curr = curr->next;
		}
		printk("\n");
		head = head->lower;
	}
}

struct skiplist_node *skiplist_last(struct skiplist *sl) {
    struct skiplist_node *curr = sl->head;

    while (curr->lower) {
        curr = curr->lower;
    }

    while (curr->next && curr->next->key != TAIL_KEY) {
        curr = curr->next;
    }

    return curr;
}

struct skiplist_node *skiplist_prev(struct skiplist *sl, sector_t key) {
    struct skiplist_node *curr = sl->head;

    while (curr) {
        while (curr->next && curr->next->key < key) {
            curr = curr->next;
        }
        
        if (!curr->lower) {
            return curr;
        }

        curr = curr->lower;
    }

    return NULL;
}
