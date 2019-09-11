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

#pragma once

typedef struct fdtgen_context fdtgen_context_t;

/**
* initialize a new fdt generation context
* @param buf, the buffer to store the generated fdt
* @param bufsize, size of the buffer
* @return the context object
*/
fdtgen_context_t *fdtgen_new_context(void *buf, size_t bufsize);

/**
* free a fdt generation context
* @param context
*/
void fdtgen_free_context(fdtgen_context_t *context);

/**
* add a list of nodes to keep, this function can be called multiply times for
* the same context object
* @param context
* @param nodes_to_keep, a list of full paths
* @param num_nodes, the number of full paths in the list
*/
void fdtgen_keep_nodes(fdtgen_context_t *context, const char **nodes_to_keep, int num_nodes);

/**
* generate a fdt
* @param context
* @param ori_fdt, the base fdt
* @return -1 on error, 0 if nonblocking and insufficient data is available, or $size
*/
void fdtgen_generate(fdtgen_context_t *context, const void *ori_fdt);

/* NOTE: the helper functions below need to be called after calling fdtgen_generate */

/**
* keep a node and all its children
* @param context
* @param ori_fdt, the base fdt
* @param node, the node to keep
*/
void fdtgen_keep_node_and_children(fdtgen_context_t *context, const void *ori_fdt, const char *node);

/**
* generate a "memory" node
* @param context
* @param base, the base of the memory region
* @param size, the size of the memory region
*/
void fdtgen_generate_memory_node(fdtgen_context_t *context, unsigned long base, size_t size);

/**
* generate a "chosen" node
* @param context
* @param stdout_path, the path of the stdout
* @param bootargs
*/
void fdtgen_generate_chosen_node(fdtgen_context_t *context, const char *stdout_path, const char *bootargs);

/**
* append the chosen node with initrd info
* @param context
* @param base, the base of the initrd image
* @param size, the size of the initrd image
*/
void fdtgen_append_chosen_node_with_initrd_info(fdtgen_context_t *context, unsigned long base, size_t size);
