/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

typedef struct fdtgen_context fdtgen_context_t;

/**
* initialize a new fdt generation context
* @param buf, the buffer to store the generated fdt
* @param bufsize, size of the buffer
* @return the context object, NULL when failed to allocate
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
void fdtgen_keep_nodes_and_disable(fdtgen_context_t *handle, const char **nodes_to_keep, int num_nodes);

/**
* generate a fdt
* @param context
* @param ori_fdt, the base fdt
* @return -1 on error, 0 otherwise
*/
int fdtgen_generate(fdtgen_context_t *context, const void *ori_fdt);

/**
* keep a node and all its children
* @param context
* @param ori_fdt, the base fdt
* @param node, the node to keep
*/
void fdtgen_keep_node_subtree(fdtgen_context_t *context, const void *ori_fdt, const char *node);
void fdtgen_keep_node_subtree_disable(fdtgen_context_t *handle, const void *ori_fdt, const char *node);
