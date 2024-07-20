#include "bptree.c"

void enqueue(node * new_node);
node *dequeue(void);
int height(node * const root);
int path_to_root(node * const root, node * child);
void print_leaves(node * const root);
void print_tree(node * const root);
void find_and_print(node * const root, int key, bool verbose); 
void find_and_print_range(node * const root, int range1, int range2, bool verbose); 
int find_range(node * const root, int key_start, int key_end, bool verbose, int returned_keys[], void * returned_pointers[]); 
node *find_leaf(node * const root, int key, bool verbose);
record *find(node * root, int key, bool verbose, node ** leaf_out);
int cut(int length);

// Insertion.

record *make_record(int value);
node *make_node(void);
node *make_leaf(void);
int get_left_index(node * parent, node * left);
node *insert_into_leaf(node * leaf, int key, record * pointer);
node *insert_into_leaf_after_splitting(node * root, node * leaf, int key, record * pointer);
node *insert_into_node(node * root, node * parent, int left_index, int key, node * right);
node *insert_into_node_after_splitting(node * root, node * parent, int left_index, int key, node * right);
node *insert_into_parent(node * root, node * left, int key, node * right);
node *insert_into_new_root(node * left, int key, node * right);
node *start_new_tree(int key, record * pointer);
node *insert(node * root, int key, int value);

// Deletion.

int get_neighbor_index(node * n);
node *adjust_root(node * root);
node *coalesce_nodes(node * root, node * n, node * neighbor, int neighbor_index, int k_prime);
node *redistribute_nodes(node * root, node * n, node * neighbor, int neighbor_index, int k_prime_index, int k_prime);
node *delete_entry(node * root, node * n, int key, void * pointer);
node *delete(node * root, int key);
