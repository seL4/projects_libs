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
#include <fdtgen.h>

typedef struct {
    char *name;
    int offset;
    UT_hash_handle hh;
} path_node_t;

static const char *props_with_dep[] = {"phy-handle", "next-level-cache", "interrupt-parent", "interrupts-extended", "clocks", "power-domains"};
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
    list_t *to_list;
    UT_hash_handle hh;
} dependency_t;

static dependency_t *d_table = NULL;

struct fdtgen_context {
    path_node_t *nodes_table;
    path_node_t *keep_node;
    dependency_t *dep_table;
    int root_offset;
    void *buffer;
    int bufsize;
    char *string_buf;
};
typedef struct fdtgen_context fdtgen_context_t;


static void init_keep_node(fdtgen_context_t *handle, const char **nodes, int num_nodes)
{
    for (int i = 0; i < num_nodes; ++i) {
        path_node_t *this = NULL;
        HASH_FIND_STR(handle->keep_node, nodes[i], this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(nodes[i]);
            HASH_ADD_STR(handle->keep_node, name, new);
        }
    }
}

static bool is_to_keep(fdtgen_context_t *handle, int offset)
{
    void *dtb = handle->buffer;
    fdt_get_path(dtb, offset, handle->string_buf, 4096);
    path_node_t *this;
    HASH_FIND_STR(handle->keep_node, handle->string_buf, this);
    return this != NULL;
}

static int print_d_node(void *node)
{
    d_list_node_t *temp = node;
    printf("\t\t to %s\n", temp->to_path);
    return 0;
}

static void inspect_dependency_list(fdtgen_context_t *handle)
{
    printf("\nInspecting the dependency list\n");
    dependency_t *tmp, *el;
    HASH_ITER(hh, handle->dep_table, el, tmp) {
        printf("From %s\n", el->from_path);
        list_foreach(el->to_list, print_d_node);
    }
}

static void inspect_keep_list(fdtgen_context_t *handle)
{
    printf("\nInspecting the keep list\n");
    path_node_t *tmp, *el;
    HASH_ITER(hh, handle->nodes_table, el, tmp) {
        printf("keep %s\n", el->name);
    }
}

static int retrive_to_phandle(const void *prop_data, int lenp)
{
    uint32_t handle = fdt32_ld(prop_data);
    return handle;
}

static void register_node_dependencies(fdtgen_context_t *handle, int offset);

static void keep_node_and_parents(fdtgen_context_t *handle,  int offset)
{
    void *dtb = handle->buffer;
    if (offset == handle->root_offset) {
        return;
    }

    fdt_get_path(dtb, offset, handle->string_buf, 4096);
    path_node_t *target;
    HASH_FIND_STR(handle->nodes_table, handle->string_buf, target);

    if (target == NULL) {
        target = malloc(sizeof(path_node_t));
        target->name = strdup(handle->string_buf);
        target->offset = offset;
        HASH_ADD_STR(handle->nodes_table, name, target);
    }

    keep_node_and_parents(handle, fdt_parent_offset(dtb, offset));
}

static void register_single_dependency(fdtgen_context_t *handle,  int offset, int lenp, const void *data,
                                       dependency_t *this)
{
    void *dtb = handle->buffer;
    d_list_node_t *new_node = malloc(sizeof(d_list_node_t));
    uint32_t to_phandle = retrive_to_phandle(data, lenp);
    int off = fdt_node_offset_by_phandle(dtb, to_phandle);
    fdt_get_path(dtb, off, handle->string_buf, 4096);
    new_node->to_path = strdup(handle->string_buf);
    new_node->to_phandle = to_phandle;

    // it is the same node when it refers to itself
    if (offset == off || list_exists(this->to_list, new_node, dnode_cmp)) {
        free(new_node->to_path);
        free(new_node);
    } else {
        list_append(this->to_list, new_node);
        keep_node_and_parents(handle, off);
        register_node_dependencies(handle, off);
    }
}

static void register_clocks_dependency(fdtgen_context_t *handle,  int offset, int lenp, const void *data_,
                                       dependency_t *this)
{
    void *dtb = handle->buffer;
    const void *data = data_;
    int done = 0;
    while (lenp > done) {
        data = (data_ + done);
        int phandle = fdt32_ld(data);
        int refers_to = fdt_node_offset_by_phandle(dtb, phandle);
        int len;
        const void *clock_cells = fdt_getprop(dtb, refers_to, "#clock-cells", &len);
        int cells = fdt32_ld(clock_cells);

        register_single_dependency(handle,  offset, lenp, data, this);

        done += 4 + cells * 4;
    }
}

static void register_power_domains_dependency(fdtgen_context_t *handle,  int offset, int lenp, const void *data_,
                                              dependency_t *this)
{
    void *dtb = handle->buffer;
    const void *data = data_;
    int done = 0;
    while (lenp > done) {
        data = (data_ + done);
        register_single_dependency(handle,  offset, lenp, data, this);
        done += 4;
    }
}

static void register_node_dependency(fdtgen_context_t *handle, int offset, const char *type)
{
    void *dtb = handle->buffer;
    int lenp = 0;
    const void *data = fdt_getprop(dtb, offset, type, &lenp);
    if (lenp < 0) {
        return;
    }
    fdt_get_path(dtb, offset, handle->string_buf, 4096);
    dependency_t *this;
    HASH_FIND_STR(handle->dep_table, handle->string_buf, this);

    if (this == NULL) {
        dependency_t *new = malloc(sizeof(dependency_t));
        new->from_path = strdup(handle->string_buf);
        new->to_list = malloc(sizeof(list_t));
        list_init(new->to_list);
        this = new;
        HASH_ADD_STR(handle->dep_table, from_path, this);
    }

    if (strcmp(type, "clocks") == 0) {
        register_clocks_dependency(handle,  offset, lenp, data, this);
    } else if (strcmp(type, "power-domains") == 0) {
        register_power_domains_dependency(handle,  offset, lenp, data, this);
    } else {
        register_single_dependency(handle, offset, lenp, data, this);
    }
}

static void register_node_dependencies(fdtgen_context_t *handle, int offset)
{
    for (int i = 0; i < num_props_with_dep; i++) {
        register_node_dependency(handle, offset, props_with_dep[i]);
    }
}

static void resolve_all_dependencies(fdtgen_context_t *handle)
{
    path_node_t *tmp, *el;
    HASH_ITER(hh, handle->nodes_table, el, tmp) {
        register_node_dependencies(handle, el->offset);
    }
}

/*
 * prefix traverse the device tree
 * keep the parent if the child is kept
 */
static int find_nodes_to_keep(fdtgen_context_t *handle, int offset)
{
    void *dtb = handle->buffer;
    int child;
    int find = 0;
    fdt_for_each_subnode(child, dtb, offset) {
        int child_is_kept = find_nodes_to_keep(handle, child);
        int in_keep_list = is_to_keep(handle, child);

        if (in_keep_list || child_is_kept) {
            find = 1;
            fdt_get_path(dtb, child, handle->string_buf, 4096);
            path_node_t *this;
            HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);

            if (this == NULL) {
                path_node_t *new = malloc(sizeof(path_node_t));
                new->offset = child;
                new->name = strdup(handle->string_buf);
                HASH_ADD_STR(handle->nodes_table, name, new);
            }
        }
    }

    return find;
}

static void trim_tree(fdtgen_context_t *handle, int offset)
{
    int child;
    void *dtb = handle->buffer;
    fdt_for_each_subnode(child, dtb, offset) {
        fdt_get_path(dtb, child, handle->string_buf, 4096);
        path_node_t *this;

        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            int err = fdt_del_node(dtb, child);
            ZF_LOGF_IF(err != 0, "Failed to delete a node from device tree");
            /* NOTE: after deleting a node, all the offsets are invalidated,
             * we need to repeat this triming process for the same node if
             * we don't want to miss anything */
            trim_tree(handle, offset);
            return;
        } else {
            trim_tree(handle, child);
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

static void clean_up(fdtgen_context_t *handle)
{
    dependency_t *tmp, *el;
    HASH_ITER(hh, handle->dep_table, el, tmp) {
        HASH_DEL(handle->dep_table, el);
        free_list(el->to_list);
        free(el->to_list);
        free(el->from_path);
        free(el);
    }

    path_node_t *tmp1, *el1;
    HASH_ITER(hh, handle->nodes_table, el1, tmp1) {
        HASH_DEL(handle->nodes_table, el1);
        free(el1->name);
        free(el1);
    }

    HASH_ITER(hh, handle->keep_node, el1, tmp1) {
        HASH_DEL(handle->keep_node, el1);
        free(el1->name);
        free(el1);
    }

    free(handle->string_buf);
}

void fdtgen_keep_nodes(fdtgen_context_t *handle, const char **nodes_to_keep, int num_nodes)
{
    init_keep_node(handle, nodes_to_keep, num_nodes);
}

static void keep_node_and_children(fdtgen_context_t *handle, const void *ori_fdt, int offset)
{
    int child;
    fdt_for_each_subnode(child, ori_fdt, offset) {
        fdt_get_path(ori_fdt, child, handle->string_buf, 4096);
        path_node_t *this;
        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(handle->string_buf);
            HASH_ADD_STR(handle->keep_node, name, new);
        }
        keep_node_and_children(handle, ori_fdt, child);
    }
}

void fdtgen_keep_node_and_children(fdtgen_context_t *handle, const void *ori_fdt, const char *node)
{
    keep_node_and_children(handle, ori_fdt, handle->root_offset);
}

int fdtgen_generate(fdtgen_context_t *handle, const void *fdt_ori)
{
    if (handle == NULL) {
        return -1;
    }
    void *fdt_gen = handle->buffer;
    fdt_open_into(fdt_ori, fdt_gen, handle->bufsize);
    /* just make sure the device tree is valid */
    int rst = fdt_check_full(fdt_gen, handle->bufsize);
    if (rst != 0) {
        ZF_LOGD("The fdt is illegal");
        return -1;
    }

    /* in case the root node is not at 0 offset.
     * is that possible? */
    handle->root_offset = fdt_path_offset(fdt_gen, "/");

    find_nodes_to_keep(handle, handle->root_offset);
    resolve_all_dependencies(handle);

    // always keep the root node
    path_node_t *root = malloc(sizeof(path_node_t));
    root->name = strdup("/");
    root->offset = handle->root_offset;
    HASH_ADD_STR(handle->nodes_table, name, root);

    trim_tree(handle, handle->root_offset);
    rst = fdt_check_full(fdt_gen, handle->bufsize);
    if (rst != 0) {
        ZF_LOGD("The generated fdt is illegal");
        return -1;
    }

    return 0;
}

static int append_prop_with_cells(fdtgen_context_t *handle, int offset,  uint64_t val, int num_cells, const char *name)
{
    int err;
    if (num_cells == 2) {
        err = fdt_appendprop_u64(handle->buffer, offset, name, val);
    } else if (num_cells == 1) {
        err = fdt_appendprop_u32(handle->buffer, offset, name, val);
    } else {
        ZF_LOGF("non-supported arch");
    }

    return err;
}

int fdtgen_generate_memory_node(fdtgen_context_t *handle, unsigned long base, size_t size)
{
    int address_cells = fdt_address_cells(handle->buffer, handle->root_offset);
    int size_cells = fdt_address_cells(handle->buffer, handle->root_offset);
    void *fdt = handle->buffer;

    int this = fdt_add_subnode(fdt, handle->root_offset, "memory");
    int err = fdt_appendprop_string(fdt, this, "device_type", "memory");
    if (err) {
        return -1;
    }
    err = append_prop_with_cells(handle, this, base, address_cells, "reg");
    if (err) {
        return -1;
    }
    err = append_prop_with_cells(handle, this, size, size_cells, "reg");
    if (err) {
        return -1;
    }

    return 0;
}

int fdtgen_generate_chosen_node(fdtgen_context_t *handle, const char *stdout_path, const char *bootargs)
{
    void *fdt = handle->buffer;
    int this = fdt_add_subnode(fdt, handle->root_offset, "chosen");
    int err = fdt_appendprop_string(fdt, this, "stdout-path", stdout_path);
    if (err) {
        return -1;
    }
    err = fdt_appendprop_string(fdt, this, "bootargs", bootargs);
    if (err) {
        return -1;
    }
    err = fdt_appendprop_string(fdt, this, "linux,stdout-path", stdout_path);
    if (err) {
        return -1;
    }

    return 0;
}

int fdtgen_append_chosen_node_with_initrd_info(fdtgen_context_t *handle, unsigned long base, size_t size)
{
    int address_cells = fdt_address_cells(handle->buffer, handle->root_offset);
    void *fdt = handle->buffer;
    int this = fdt_path_offset(fdt, "/chosen");
    int err = append_prop_with_cells(handle, this, base, address_cells, "linux,initrd-start");
    if (err) {
        return -1;
    }
    err = append_prop_with_cells(handle, this, base + size, address_cells, "linux,initrd-end");
    if (err) {
        return -1;
    }

    return 0;
}

fdtgen_context_t *fdtgen_new_context(void *buf, size_t bufsize)
{
    fdtgen_context_t *to_return = malloc(sizeof(fdtgen_context_t));
    if (to_return == NULL) {
        return NULL;
    }
    to_return->buffer = buf;
    to_return->bufsize = bufsize;
    to_return->nodes_table = NULL;
    to_return->keep_node = NULL;
    to_return->dep_table = NULL;
    to_return->root_offset = 0;
    to_return->string_buf = malloc(4096);
    return to_return;
}

void fdtgen_free_context(fdtgen_context_t *h)
{
    if (h) {
        clean_up(h);
        free(h);
    }
}
