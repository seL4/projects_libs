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
#include <utils/uthash.h>
#include <fdtgen.h>

#define MAX_FULL_PATH_LENGTH (4096)

enum device_flag {
    DEVICE_KEEP = 1,
    DEVICE_KEEP_AND_DISABLE = 2,
};

typedef struct {
    char *name;
    int offset;
    int cnt;
    enum device_flag flag;
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

struct fdtgen_context {
    path_node_t *nodes_table;
    dependency_t *dep_table;
    int root_offset;
    void *buffer;
    int bufsize;
    char *string_buf;
};
typedef struct fdtgen_context fdtgen_context_t;

static void init_keep_node(fdtgen_context_t *handle, const char **nodes, int num_nodes, enum device_flag flag)
{
    for (int i = 0; i < num_nodes; ++i) {
        path_node_t *this = NULL;
        HASH_FIND_STR(handle->nodes_table, nodes[i], this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(nodes[i]);
            new->flag = flag;
            new->cnt = 0;
            HASH_ADD_STR(handle->nodes_table, name, new);
        } else {
            this->flag = flag;
        }
    }
}

static int is_to_keep(fdtgen_context_t *handle, UNUSED int child)
{
    void *dtb = handle->buffer;
    path_node_t *this;
    HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);

    return this != NULL;
}

static int retrive_to_phandle(const void *prop_data, int lenp)
{
    uint32_t handle = fdt32_ld(prop_data);
    return handle;
}

static void register_node_dependencies(fdtgen_context_t *handle, int offset);

static int keep_node_and_parents(fdtgen_context_t *handle,  int offset, int target)
{
    void *dtb = handle->buffer;
    if (target == handle->root_offset) {
        return 0;
    }
    if (target == offset) {
        return 1;
    }
    int child;

    fdt_for_each_subnode(child, dtb, offset) {
        int new_len = strlen(handle->string_buf);
        strcat(handle->string_buf, "/");
        const char *n = fdt_get_name(dtb, child, NULL);
        strcat(handle->string_buf, n);
        char *curr = strdup(handle->string_buf);

        int keep = keep_node_and_parents(handle, child, target);
        if (keep) {
            path_node_t *target_node = NULL;
            HASH_FIND_STR(handle->nodes_table, curr, target_node);
            if (target_node == NULL) {
                target_node = malloc(sizeof(path_node_t));
                target_node->name = strdup(curr);
                target_node->offset = offset;
                target_node->cnt = 0;
                target_node->flag = DEVICE_KEEP;
                HASH_ADD_STR(handle->nodes_table, name, target_node);
            }

            dependency_t *dep;
            HASH_FIND_STR(handle->dep_table, curr, dep);
            if (dep == NULL) {
                register_node_dependencies(handle, child);
            }
            handle->string_buf[new_len] = '\0';
            return 1;
        }

        handle->string_buf[new_len] = '\0';
        free(curr);
    }

    return 0;
}

static void register_single_dependency(fdtgen_context_t *handle,  int offset, int lenp, const void *data,
                                       dependency_t *this)
{
    void *dtb = handle->buffer;
    d_list_node_t *new_node = malloc(sizeof(d_list_node_t));
    uint32_t to_phandle = retrive_to_phandle(data, lenp);
    int off = fdt_node_offset_by_phandle(dtb, to_phandle);
    fdt_get_path(dtb, off, handle->string_buf, MAX_FULL_PATH_LENGTH);
    new_node->to_path = strdup(handle->string_buf);
    new_node->to_phandle = to_phandle;

    // it is the same node when it refers to itself
    if (offset == off || list_exists(this->to_list, new_node, dnode_cmp)) {
        free(new_node->to_path);
        free(new_node);
    } else {
        list_append(this->to_list, new_node);
        handle->string_buf[0] = '\0';
        keep_node_and_parents(handle, handle->root_offset, off);
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

        register_single_dependency(handle, offset, lenp, data, this);

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
        register_single_dependency(handle, offset, lenp, data, this);
        done += 4;
    }
}

static void register_node_dependency(fdtgen_context_t *handle, int offset, const char *type, int p_offset)
{
    void *dtb = handle->buffer;
    int lenp = 0;
    const void *data = fdt_getprop_by_offset(dtb, p_offset, NULL, &lenp);
    fdt_get_path(dtb, offset, handle->string_buf, MAX_FULL_PATH_LENGTH);
    dependency_t *this = NULL;
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
        register_clocks_dependency(handle, offset, lenp, data, this);
    } else if (strcmp(type, "power-domains") == 0) {
        register_power_domains_dependency(handle, offset, lenp, data, this);
    } else {
        register_single_dependency(handle, offset, lenp, data, this);
    }
}

static void register_node_dependencies(fdtgen_context_t *handle, int offset)
{
    if (offset == handle->root_offset) {
        return;
    }
    int prop_off, lenp;
    void *dtb = handle->buffer;

    fdt_for_each_property_offset(prop_off, dtb, offset) {
        const struct fdt_property *prop = fdt_get_property_by_offset(dtb, prop_off, NULL);
        const char *name = fdt_get_string(dtb, fdt32_ld(&prop->nameoff), &lenp);
        for (int i = 0; i < num_props_with_dep; i++) {
            if (strcmp(name, props_with_dep[i]) == 0) {
                register_node_dependency(handle, offset, name, prop_off);
            }
        }
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
        int len_ori = strlen(handle->string_buf);
        strcat(handle->string_buf, "/");
        const char *n = fdt_get_name(dtb, child, NULL);
        strcat(handle->string_buf, n);

        int child_is_kept = find_nodes_to_keep(handle, child);
        int in_keep_list = 0;
        if (child_is_kept == 0) {
            in_keep_list = is_to_keep(handle, child);
        }

        if (in_keep_list || child_is_kept) {
            find = 1;
            path_node_t *this;
            HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);

            if (this == NULL) { /* this is not in the keep list */
                path_node_t *new = malloc(sizeof(path_node_t));
                new->offset = child;
                new->name = strdup(handle->string_buf);
                new->cnt = 0;
                new->flag = DEVICE_KEEP;
                HASH_ADD_STR(handle->nodes_table, name, new);
            } else {
                this->offset = child;
            }
        }

        handle->string_buf[len_ori] = '\0';
    }

    return find;
}

static void trim_tree(fdtgen_context_t *handle, int offset)
{
    int child;
    void *dtb = handle->buffer;

    fdt_for_each_subnode(child, dtb, offset) {
        int len_ori = strlen(handle->string_buf);
        const char *n = fdt_get_name(dtb, child, NULL);
        strcat(handle->string_buf, "/");
        strcat(handle->string_buf, n);
        path_node_t *this;

        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            int err = fdt_del_node(dtb, child);
            ZF_LOGF_IF(err != 0, "Failed to delete a node from device tree");
            /* NOTE: after deleting a node, all the offsets are invalidated,
             * we need to repeat this triming process for the same node if
             * we don't want to miss anything */
            handle->string_buf[len_ori] = '\0';
            trim_tree(handle, offset);
            return;
        } else {
            this->cnt++;

            if (this->flag == DEVICE_KEEP_AND_DISABLE && this->cnt == 1) {
                int err = fdt_setprop_string(dtb, child, "status", "disabled");
                ZF_LOGF_IF(err, "failed, %d", err);
                handle->string_buf[len_ori] = '\0';
                trim_tree(handle, offset);
                return;
            } else {
                trim_tree(handle, child);
            }
        }
        handle->string_buf[len_ori] = '\0';
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
        if (el1->cnt == 0) {
            ZF_LOGE("Non-existing node %s specified to be kept", el1->name);
        }
        HASH_DEL(handle->nodes_table, el1);
        free(el1->name);
        free(el1);
    }

    free(handle->string_buf);
}

void fdtgen_keep_nodes(fdtgen_context_t *handle, const char **nodes_to_keep, int num_nodes)
{
    init_keep_node(handle, nodes_to_keep, num_nodes, DEVICE_KEEP);
}

void fdtgen_keep_nodes_and_disable(fdtgen_context_t *handle, const char **nodes_to_keep, int num_nodes)
{
    init_keep_node(handle, nodes_to_keep, num_nodes, DEVICE_KEEP_AND_DISABLE);
}

static void keep_node_and_children(fdtgen_context_t *handle, const void *ori_fdt, int offset, enum device_flag flag)
{
    int child;
    fdt_for_each_subnode(child, ori_fdt, offset) {
        int len_ori = strlen(handle->string_buf);
        strcat(handle->string_buf, "/");
        const char *n = fdt_get_name(ori_fdt, child, NULL);
        strcat(handle->string_buf, n);

        path_node_t *this;
        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(handle->string_buf);
            new->cnt = 0;
            new->flag = flag;
            HASH_ADD_STR(handle->nodes_table, name, new);
        } else {
            this->flag = flag;
        }

        keep_node_and_children(handle, ori_fdt, child, flag);
        handle->string_buf[len_ori] = '\0';
    }
}

void fdtgen_keep_node_subtree_disable(fdtgen_context_t *handle, const void *ori_fdt, const char *node)
{
    int child = fdt_path_offset(ori_fdt, node);
    if (child < 0) {
        ZF_LOGE("Non-existing root node %s", node);
    } else {
        fdt_get_path(ori_fdt, child, handle->string_buf, MAX_FULL_PATH_LENGTH);
        path_node_t *this;
        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(handle->string_buf);
            new->cnt = 0;
            new->flag = DEVICE_KEEP_AND_DISABLE;
            HASH_ADD_STR(handle->nodes_table, name, new);
        } else {
            this->flag = DEVICE_KEEP_AND_DISABLE;
        }
        keep_node_and_children(handle, ori_fdt, child, DEVICE_KEEP_AND_DISABLE);
    }
}


void fdtgen_keep_node_subtree(fdtgen_context_t *handle, const void *ori_fdt, const char *node)
{
    int child = fdt_path_offset(ori_fdt, node);
    if (child < 0) {
        ZF_LOGE("Non-existing root node %s", node);
    } else {
        fdt_get_path(ori_fdt, child, handle->string_buf, MAX_FULL_PATH_LENGTH);
        path_node_t *this;
        HASH_FIND_STR(handle->nodes_table, handle->string_buf, this);
        if (this == NULL) {
            path_node_t *new = malloc(sizeof(path_node_t));
            new->name = strdup(handle->string_buf);
            new->cnt = 0;
            new->flag = DEVICE_KEEP;
            HASH_ADD_STR(handle->nodes_table, name, new);
        } else {
            this->flag = DEVICE_KEEP;
        }
        keep_node_and_children(handle, ori_fdt, child, DEVICE_KEEP);
    }
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
        ZF_LOGE("The original fdt is illegal : %d", rst);
        return -1;
    }

    /* in case the root node is not at 0 offset.
     * is that possible? */
    handle->root_offset = fdt_path_offset(fdt_gen, "/");

    handle->string_buf[0] = '\0';
    find_nodes_to_keep(handle, handle->root_offset);
    resolve_all_dependencies(handle);

    // always keep the root node
    path_node_t *root = malloc(sizeof(path_node_t));
    root->name = strdup("/");
    root->offset = handle->root_offset;
    root->cnt = 1;
    root->flag = DEVICE_KEEP;
    HASH_ADD_STR(handle->nodes_table, name, root);

    handle->string_buf[0] = '\0';
    trim_tree(handle, handle->root_offset);
    rst = fdt_check_full(fdt_gen, handle->bufsize);
    if (rst != 0) {
        ZF_LOGE("The generated fdt is illegal");
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
    to_return->dep_table = NULL;
    to_return->root_offset = 0;
    to_return->string_buf = malloc(MAX_FULL_PATH_LENGTH);
    return to_return;
}

void fdtgen_free_context(fdtgen_context_t *h)
{
    if (h) {
        clean_up(h);
        free(h);
    }
}
