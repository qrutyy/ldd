// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#define HT_MAP_BITS 7
#define CHUNK_SIZE 1024 * 1024
#define BUCKET_NUM (sector_t)(key / (CHUNK_SIZE))

struct hashmap {
	DECLARE_HASHTABLE(head, HT_MAP_BITS);
	struct hash_el* last_el;
};

struct hash_el {
	sector_t key;
	void* value;
	struct hlist_node node;
};

void hash_add_cs(struct hlist_head *hm_head, struct hlist_node *node, sector_t key);
void hashmap_free(struct hashmap *hm);
struct hash_el* hashmap_find_node(struct hashmap *hm, sector_t key);
struct hash_el* hashmap_prev(struct hashmap *hm, sector_t key);

