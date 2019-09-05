/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <stdio.h>
#include <stdbool.h>

#include <libfdt.h>
#include <utils/list.h>
#include <utils/util.h>
#include "uthash.h"

typedef struct {
    char *name;
    int offset;
    UT_hash_handle hh;
} path_node_t;
static path_node_t *nodes_table = NULL;

static path_node_t *keep_node = NULL;

static char *tempbuf;

static int root_offset;

static void init_keep_node(const char **nodes, int num_nodes)
{
    for (int i = 0; i < num_nodes; ++i) {
        path_node_t *new = malloc(sizeof(path_node_t));
        new->name = strdup(nodes[i]);
        HASH_ADD_STR(keep_node, name, new);
    }
}

static bool is_to_keep(void *dtb, int offset)
{
    fdt_get_path(dtb, offset, tempbuf, 4096);
    path_node_t *this;
    HASH_FIND_STR(keep_node, tempbuf, this);
    return this != NULL;
}


static const char *props_with_dep[] = {"phy-handle", "next-level-cache", "interrupt-parent", "interrupts-extended", "clocks"};
static const int num_props_with_dep = sizeof(props_with_dep) / sizeof(char *);

typedef struct {
    char *to_path;
    uint32_t to_phandle;
} d_list_node_t;

static int dnode_cmp(void *_a, void *_b)
{
    d_list_node_t *a = _a, *b = _b;
    return strcmp(a->to_path, b->to_path);
}

typedef struct {
    char *from_path;
    list_t *d_list;
    UT_hash_handle hh;
} dependency_t;

static dependency_t *d_table = NULL;

static int print_d_node(void *node)
{
    d_list_node_t *temp = node;
    printf("\t\t to %s\n", temp->to_path);
    return 0;
}

static void inspect_dependency_list(void)
{
    printf("\nInspecting the dependency list\n");
    dependency_t *tmp, *el;
    HASH_ITER(hh, d_table, el, tmp) {
        printf("From %s\n", el->from_path);
        list_foreach(el->d_list, print_d_node);
    }
}

static void inspect_keep_list(void)
{
    printf("\nInspecting the keep list\n");
    path_node_t *tmp, *el;
    HASH_ITER(hh, nodes_table, el, tmp) {
        printf("keep %s\n", el->name);
    }
}

static int retrive_to_phandle(const void *prop_data, int lenp, const char *type)
{
    // TODO: this is ugly
    if (lenp == 32) {
        // this case the only type is interrupt-extended
        ZF_LOGF_IF(strcmp(type, "interrupts-extended") != 0, "type length mismatch");
    } else if (lenp > 4) {
        // this case the only type is clocks, and it has variable length
        ZF_LOGF_IF(strcmp(type, "clocks") != 0, "type length mismatch");
    }

    uint32_t handle = fdt32_ld(prop_data);
    return handle;
}

static void keep_node_and_parents(void *dtb, int offset)
{
    if (offset == root_offset) {
        return;
    }

    fdt_get_path(dtb, offset, tempbuf, 4096);
    path_node_t *target;
    HASH_FIND_STR(nodes_table, tempbuf, target);

    if (target == NULL) {
        target = malloc(sizeof(path_node_t));
        target->name = strdup(tempbuf);
        target->offset = offset;
        HASH_ADD_STR(nodes_table, name, target);
    }

    keep_node_and_parents(dtb, fdt_parent_offset(dtb, offset));
}

static void register_dependency(void *dtb, int offset)
{
    for (int i = 0; i < num_props_with_dep; i++) {
        int lenp = 0;
        const void *data = fdt_getprop(dtb, offset, props_with_dep[i], &lenp);

        if (lenp >= 0) { // we got the property we want
            fdt_get_path(dtb, offset, tempbuf, 4096);
            dependency_t *this;
            HASH_FIND_STR(d_table, tempbuf, this);
            if (this == NULL) {
                dependency_t *new = malloc(sizeof(dependency_t));
                new->from_path = strdup(tempbuf);
                new->d_list = malloc(sizeof(list_t));
                list_init(new->d_list);
                this = new;
                HASH_ADD_STR(d_table, from_path, this);
            }


            d_list_node_t *new_node = malloc(sizeof(d_list_node_t));
            uint32_t to_phandle = retrive_to_phandle(data, lenp, props_with_dep[i]);
            int off = fdt_node_offset_by_phandle(dtb, to_phandle);
            fdt_get_path(dtb, off, tempbuf, 4096);
            new_node->to_path = strdup(tempbuf);
            new_node->to_phandle = to_phandle;

            // it is the same node when it refers to itself
            if (offset == off || list_exists(this->d_list, new_node, dnode_cmp)) {
                free(new_node->to_path);
                free(new_node);
            } else {
                list_append(this->d_list, new_node);
                keep_node_and_parents(dtb, off);
                register_dependency(dtb, off);
            }
        }
    }
}

static void resolve_dependency(void *dtb)
{
    path_node_t *tmp, *el;
    HASH_ITER(hh, nodes_table, el, tmp) {
        register_dependency(dtb, el->offset);
    }
}

/*
 * prefix traverse the device tree
 * keep the parent if the child is kept
 */
static int find_nodes_to_keep(void *dtb, int offset)
{
    int child;
    int find = 0;
    fdt_for_each_subnode(child, dtb, offset) {
        int child_is_kept = find_nodes_to_keep(dtb, child);
        int in_keep_list = is_to_keep(dtb, child);

        if (in_keep_list || child_is_kept) {
            find = 1;
            fdt_get_path(dtb, child, tempbuf, 4096);
            path_node_t *this;
            HASH_FIND_STR(nodes_table, tempbuf, this);

            if (this == NULL) {
                path_node_t *new = malloc(sizeof(path_node_t));
                new->offset = child;
                new->name = strdup(tempbuf);
                HASH_ADD_STR(nodes_table, name, new);
            }
        }
    }

    return find;
}

static void trim_tree(void *dtb, int offset)
{
    int child;
    fdt_for_each_subnode(child, dtb, offset) {
        fdt_get_path(dtb, child, tempbuf, 4096);
        path_node_t *this;

        HASH_FIND_STR(nodes_table, tempbuf, this);
        if (this == NULL) {
            int err = fdt_del_node(dtb, child);
            ZF_LOGF_IF(err != 0, "Failed to delete a node from device tree");
            /* NOTE: after deleting a node, all the offsets are invalidated,
             * we need to repeat this triming process for the same node if
             * we don't want to miss anything */
            trim_tree(dtb, offset);
            return;
        } else {
            trim_tree(dtb, child);
        }
    }
}

static void free_list(list_t *l)
{
    struct list_node *a = l->head;
    while (a != NULL) {
        struct list_node *next = a->next;
        d_list_node_t *node = a->data;
        free(node->to_path);
        free(node);
        a = next;
    }

    list_remove_all(l);
}

static void clean_up()
{
    dependency_t *tmp, *el;
    HASH_ITER(hh, d_table, el, tmp) {
        HASH_DEL(d_table, el);
        free_list(el->d_list);
        free(el->d_list);
        free(el->from_path);
        free(el);
    }

    path_node_t *tmp1, *el1;
    HASH_ITER(hh, nodes_table, el1, tmp1) {
        HASH_DEL(nodes_table, el1);
        free(el1->name);
        free(el1);
    }

    HASH_ITER(hh, keep_node, el1, tmp1) {
        HASH_DEL(keep_node, el1);
        free(el1->name);
        free(el1);
    }

    free(tempbuf);
}

void *fdt_gen(const void *fdt_ori, const char **nodes_to_keep, int num_nodes)
{
    tempbuf = malloc(4096);
    int fdtsize = fdt_totalsize(fdt_ori);
    void *fdt_gen = malloc(fdtsize);
    memcpy(fdt_gen, fdt_ori, fdtsize);
    /* just make sure the device tree is valid */
    int rst = fdt_check_full(fdt_gen, fdtsize);
    ZF_LOGF_IF(rst != 0, "The fdt is illegal");

    /* in case the root node is not at 0 offset.
     * is that possible? */
    root_offset = fdt_path_offset(fdt_gen, "/");

    init_keep_node(nodes_to_keep, num_nodes);

    find_nodes_to_keep(fdt_gen, root_offset);
    resolve_dependency(fdt_gen);

    // always keep the root node
    path_node_t *root = malloc(sizeof(path_node_t));
    root->name = strdup("/");
    root->offset = root_offset;
    HASH_ADD_STR(nodes_table, name, root);

    trim_tree(fdt_gen, root_offset);

    fdt_pack(fdt_gen);
    rst = fdt_check_full(fdt_gen, fdtsize);
    ZF_LOGF_IF(rst != 0, "The generated fdt is illegal");

    clean_up();

    return fdt_gen;
}
