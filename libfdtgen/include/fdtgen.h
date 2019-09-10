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

void *fdtgen_generate(const void *fdt);
void fdtgen_keep_nodes(const char **nodes_to_keep, int num_nodes);
void fdtgen_keep_node_and_children(const void *ori_fdt, const char *node);
void fdtgen_generate_memory_node(void *fdt, unsigned long base, size_t size);
void fdtgen_generate_chosen_node(void *fdt, const char *stdout_path, const char *bootargs);
void fdtgen_append_chosen_node_with_initrd_info(void *fdt, unsigned long base, size_t size);
