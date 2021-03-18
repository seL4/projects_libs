<!--
    Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)

    SPDX-License-Identifier: CC-BY-SA-4.0
-->

libvswitch
-----------

**_This implementation is currently a work in progress_**

This directory contains a library implementation of a vswitch. The library is
intended to provide an interface to manage and route data between components
identified by a MAC address. A use case for this library is to provide a VMM
an interface to manage a routing topology for VM-VM communication. The vswitch
library is based on 'virtqueues' (libvirtqueue), where a link between two
components is represented by a send and recieve virtqueue.

To use this library in a project you can link `vswitch` in your target
applications CMake file.
