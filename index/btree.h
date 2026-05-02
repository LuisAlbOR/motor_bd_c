#ifndef BTREE_H
#define BTREE_H

#define BTREE_ORDER 4
#define BTREE_MAX_KEYS (BTREE_ORDER - 1)

typedef struct {
    int page_id;
    int slot_id;
} BTreeRecordRef;

typedef struct BPlusNode {
    int is_leaf;
    int num_keys;

    int keys[BTREE_MAX_KEYS];

    struct BPlusNode *children[BTREE_ORDER];

    BTreeRecordRef records[BTREE_MAX_KEYS];

    struct BPlusNode *parent;
    struct BPlusNode *next;
} BPlusNode;

typedef struct {
    BPlusNode *root;
} BPlusTree;

BPlusTree* btree_create(void);
BPlusNode* btree_create_node(int is_leaf);

void btree_insert(BPlusTree *tree, int key, int page_id, int slot_id);
int btree_search(BPlusTree *tree, int key, BTreeRecordRef *result);

void btree_print(BPlusTree *tree);
void btree_free_tree(BPlusTree *tree);
void btree_free_node(BPlusNode *node);

#endif