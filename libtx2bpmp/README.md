<!--
    Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)

    SPDX-License-Identifier: CC-BY-SA-4.0
-->


libtx2bpmp
==========

This is a port of the Tegra186 Boot and Power Management Processor (BPMP)
interfaces from U-Boot to seL4.

The port consists of modifying the original sources to use the native seL4
mechanisms and libraries instead of the U-Boot mechanisms.

This library also contains drivers for the Hardware Synchronisation Primitives
(HSP) device and the Inter-VM Communication (IVC) protocol.

Refer to DESIGN.md for more information about these devices and drivers.
