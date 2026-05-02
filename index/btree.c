#include "btree.h"
#include <stdio.h>
#include <stdlib.h>

static int find_leaf_index(BPlusNode *node, int key) {
    int i = 0;
    while (i < node->num_keys && key >= node->keys[i]) {
        i++;
    }
    return i;
}

BPlusNode* btree_create_node(int is_leaf) {
    BPlusNode *node = (BPlusNode*)calloc(1, sizeof(BPlusNode));
    if (!node) {
        fprintf(stderr, "Error: no se pudo reservar memoria para nodo B+ Tree.\n");
        exit(EXIT_FAILURE);
    }

    node->is_leaf = is_leaf;
    node->num_keys = 0;
    node->parent = NULL;
    node->next = NULL;

    return node;
}

BPlusTree* btree_create(void) {
    BPlusTree *tree = (BPlusTree*)calloc(1, sizeof(BPlusTree));
    if (!tree) {
        fprintf(stderr, "Error: no se pudo reservar memoria para B+ Tree.\n");
        exit(EXIT_FAILURE);
    }

    tree->root = btree_create_node(1);
    return tree;
}

static BPlusNode* btree_find_leaf(BPlusNode *root, int key) {
    if (!root) return NULL;

    BPlusNode *current = root;

    while (!current->is_leaf) {
        int index = find_leaf_index(current, key);
        current = current->children[index];
    }

    return current;
}

int btree_search(BPlusTree *tree, int key, BTreeRecordRef *result) {
    if (!tree || !tree->root) return 0;

    BPlusNode *leaf = btree_find_leaf(tree->root, key);
    if (!leaf) return 0;

    for (int i = 0; i < leaf->num_keys; i++) {
        if (leaf->keys[i] == key) {
            if (result) {
                *result = leaf->records[i];
            }
            return 1;
        }
    }

    return 0;
}

static void insert_into_leaf(BPlusNode *leaf, int key, BTreeRecordRef record) {
    int i = leaf->num_keys - 1;

    while (i >= 0 && leaf->keys[i] > key) {
        leaf->keys[i + 1] = leaf->keys[i];
        leaf->records[i + 1] = leaf->records[i];
        i--;
    }

    leaf->keys[i + 1] = key;
    leaf->records[i + 1] = record;
    leaf->num_keys++;
}

static void insert_into_parent(BPlusTree *tree, BPlusNode *left, int key, BPlusNode *right);

static void split_leaf_and_insert(BPlusTree *tree, BPlusNode *leaf, int key, BTreeRecordRef record) {
    int temp_keys[BTREE_MAX_KEYS + 1];
    BTreeRecordRef temp_records[BTREE_MAX_KEYS + 1];

    int inserted = 0;
    int i = 0;
    int j = 0;

    while (i < leaf->num_keys) {
        if (!inserted && key < leaf->keys[i]) {
            temp_keys[j] = key;
            temp_records[j] = record;
            inserted = 1;
            j++;
        }

        temp_keys[j] = leaf->keys[i];
        temp_records[j] = leaf->records[i];
        i++;
        j++;
    }

    if (!inserted) {
        temp_keys[j] = key;
        temp_records[j] = record;
        j++;
    }

    int split = (BTREE_MAX_KEYS + 1) / 2;

    BPlusNode *new_leaf = btree_create_node(1);
    new_leaf->parent = leaf->parent;

    leaf->num_keys = 0;

    for (i = 0; i < split; i++) {
        leaf->keys[i] = temp_keys[i];
        leaf->records[i] = temp_records[i];
        leaf->num_keys++;
    }

    for (i = split, j = 0; i < BTREE_MAX_KEYS + 1; i++, j++) {
        new_leaf->keys[j] = temp_keys[i];
        new_leaf->records[j] = temp_records[i];
        new_leaf->num_keys++;
    }

    new_leaf->next = leaf->next;
    leaf->next = new_leaf;

    int promoted_key = new_leaf->keys[0];
    insert_into_parent(tree, leaf, promoted_key, new_leaf);
}

static void insert_into_internal(BPlusNode *parent, int left_index, int key, BPlusNode *right) {
    for (int i = parent->num_keys; i > left_index; i--) {
        parent->keys[i] = parent->keys[i - 1];
    }

    for (int i = parent->num_keys + 1; i > left_index + 1; i--) {
        parent->children[i] = parent->children[i - 1];
    }

    parent->keys[left_index] = key;
    parent->children[left_index + 1] = right;
    parent->num_keys++;

    if (right) {
        right->parent = parent;
    }
}

static int get_child_index(BPlusNode *parent, BPlusNode *child) {
    for (int i = 0; i <= parent->num_keys; i++) {
        if (parent->children[i] == child) {
            return i;
        }
    }

    return -1;
}

static void split_internal_and_insert(BPlusTree *tree, BPlusNode *old_node, int left_index, int key, BPlusNode *right) {
    int temp_keys[BTREE_MAX_KEYS + 1];
    BPlusNode *temp_children[BTREE_ORDER + 1];

    for (int i = 0; i <= BTREE_ORDER; i++) {
        temp_children[i] = NULL;
    }

    for (int i = 0; i < old_node->num_keys; i++) {
        temp_keys[i] = old_node->keys[i];
    }

    for (int i = 0; i <= old_node->num_keys; i++) {
        temp_children[i] = old_node->children[i];
    }

    for (int i = old_node->num_keys; i > left_index; i--) {
        temp_keys[i] = temp_keys[i - 1];
    }

    temp_keys[left_index] = key;

    for (int i = old_node->num_keys + 1; i > left_index + 1; i--) {
        temp_children[i] = temp_children[i - 1];
    }

    temp_children[left_index + 1] = right;

    int total_keys = old_node->num_keys + 1;
    int split = total_keys / 2;
    int promoted_key = temp_keys[split];

    BPlusNode *new_node = btree_create_node(0);
    new_node->parent = old_node->parent;

    old_node->num_keys = 0;

    for (int i = 0; i < BTREE_ORDER; i++) {
        old_node->children[i] = NULL;
    }

    for (int i = 0; i < split; i++) {
        old_node->keys[i] = temp_keys[i];
        old_node->children[i] = temp_children[i];

        if (old_node->children[i]) {
            old_node->children[i]->parent = old_node;
        }

        old_node->num_keys++;
    }

    old_node->children[split] = temp_children[split];
    if (old_node->children[split]) {
        old_node->children[split]->parent = old_node;
    }

    int j = 0;

    for (int i = split + 1; i < total_keys; i++, j++) {
        new_node->keys[j] = temp_keys[i];
        new_node->children[j] = temp_children[i];

        if (new_node->children[j]) {
            new_node->children[j]->parent = new_node;
        }

        new_node->num_keys++;
    }

    new_node->children[j] = temp_children[total_keys];
    if (new_node->children[j]) {
        new_node->children[j]->parent = new_node;
    }

    insert_into_parent(tree, old_node, promoted_key, new_node);
}

static void insert_into_parent(BPlusTree *tree, BPlusNode *left, int key, BPlusNode *right) {
    if (!left->parent) {
        BPlusNode *new_root = btree_create_node(0);

        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->num_keys = 1;

        left->parent = new_root;
        right->parent = new_root;

        tree->root = new_root;
        return;
    }

    BPlusNode *parent = left->parent;
    int left_index = get_child_index(parent, left);

    if (left_index == -1) {
        fprintf(stderr, "Error: inconsistencia en B+ Tree.\n");
        return;
    }

    if (parent->num_keys < BTREE_MAX_KEYS) {
        insert_into_internal(parent, left_index, key, right);
    } else {
        split_internal_and_insert(tree, parent, left_index, key, right);
    }
}

void btree_insert(BPlusTree *tree, int key, int page_id, int slot_id) {
    if (!tree) return;

    BTreeRecordRef existing;
    if (btree_search(tree, key, &existing)) {
        BPlusNode *leaf = btree_find_leaf(tree->root, key);

        for (int i = 0; i < leaf->num_keys; i++) {
            if (leaf->keys[i] == key) {
                leaf->records[i].page_id = page_id;
                leaf->records[i].slot_id = slot_id;
                return;
            }
        }
    }

    BTreeRecordRef record;
    record.page_id = page_id;
    record.slot_id = slot_id;

    BPlusNode *leaf = btree_find_leaf(tree->root, key);

    if (leaf->num_keys < BTREE_MAX_KEYS) {
        insert_into_leaf(leaf, key, record);
    } else {
        split_leaf_and_insert(tree, leaf, key, record);
    }
}

static void print_node(BPlusNode *node, int level) {
    if (!node) return;

    for (int i = 0; i < level; i++) {
        printf("  ");
    }

    printf(node->is_leaf ? "Leaf: " : "Internal: ");

    for (int i = 0; i < node->num_keys; i++) {
        printf("%d ", node->keys[i]);
    }

    printf("\n");

    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            print_node(node->children[i], level + 1);
        }
    }
}

void btree_print(BPlusTree *tree) {
    if (!tree || !tree->root) {
        printf("B+ Tree vacío.\n");
        return;
    }

    print_node(tree->root, 0);
}

void btree_free_node(BPlusNode *node) {
    if (!node) return;

    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            btree_free_node(node->children[i]);
        }
    }

    free(node);
}

void btree_free_tree(BPlusTree *tree) {
    if (!tree) return;

    btree_free_node(tree->root);
    free(tree);
}