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

typedef struct fdtgen fdtgen_t;

fdtgen_t *fdtgen_new(void *buf, size_t bufsize);
void fdtgen_cleanup(fdtgen_t *handle);
void fdtgen_generate(fdtgen_t *handle, const void *ori_fdt);
void fdtgen_keep_nodes(fdtgen_t *handle, const char **nodes_to_keep, int num_nodes);
void fdtgen_keep_node_and_children(fdtgen_t *handle, const void *ori_fdt, const char *node);
void fdtgen_generate_memory_node(fdtgen_t *handle, unsigned long base, size_t size);
void fdtgen_generate_chosen_node(fdtgen_t *handle, const char *stdout_path, const char *bootargs);
void fdtgen_append_chosen_node_with_initrd_info(fdtgen_t *handle, unsigned long base, size_t size);
