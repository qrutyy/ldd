/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Original Author: Viacheslav Sidorov @xamelllion
 * 
 * Modified by Mikhail Gavrilenko on _ 
 * Changes: Briefly describe any modifications
 * (REMOVE IF NOT MODIFIED)
 */

#pragma once

#define DEFAULT_MAP_SIZE 6
#define FILL_FACTOR (2/3.)
#define INCREASE_FACTOR (1/3.)
#define uint64_bdd unsigned long

typedef struct {
	int has_value;
	uint64_bdd key;
	uint64_bdd value;
} hashmap_value;

typedef struct {
	int size;
	int used_size;
	hashmap_value* array;
} hashmap;


/*   User methods   */
hashmap *hashmap_init(void);
void hashmap_free(hashmap *map);
hashmap_value hashmap_get(hashmap *map, uint64_bdd key);
void hashmap_put(hashmap *map, uint64_bdd key, uint64_bdd value);
void hashmap_remove(hashmap *map, int key);
void hashmap_print(hashmap *map);

/*   Service methods   */
void _increase_map_size(hashmap *map);
uint64_bdd _hash_function(hashmap *map, uint64_bdd x);
void _hashmap_insert_value(hashmap *map, uint64_bdd key, uint64_bdd value);
