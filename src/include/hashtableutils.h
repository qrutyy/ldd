// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#define HT_MAP_BITS 10

struct hashmap {
	DECLARE_HASHTABLE(head, HT_MAP_BITS);
	struct hash_el* last_el;
};

struct hash_el {
	sector_t key;
	void* value;
	struct hlist_node node;
};

void hashmap_free(struct hashmap *hm);
struct hash_el* hashmap_find_node(struct hashmap *hm, sector_t key);
struct hash_el* hashmap_last(struct hashmap *hm);
struct hash_el* hashmap_prev(struct hashmap *hm, sector_t key);

