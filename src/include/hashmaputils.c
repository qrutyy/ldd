#include <cstdint>
#include <linux/hashmap.h>

 #define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

void hashmap_free(struct hashmap *hm) {
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm, bckt_iter, el, next)
		if (el != NULL)
			hash_del(el->node)
			kfree(el);
}

struct hlist_node* hashmap_find_node(struct hashmap *hm, sector_t key) {
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm, bckt_iter, el, next)
		if (el != NULL && el->key == key)
			return el;
	return NULL;
}

struct hlist_node* hashmap_last(struct hashmap *hm) {
	hlist_node max_value_node;
	uint64_t max_key = 0;
	int bckt_iter = 0;
	struct hash_el *el;
	hash_for_each(hm, bckt_iter, el, next)
		if (el->key > max_key) {
			max_key = el->key;
			max_value_node = el->node;
		} 
	return max_value_node;	
}
