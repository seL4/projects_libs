<!--
     Copyright 2018, Data61
     Commonwealth Scientific and Industrial Research Organisation (CSIRO)
     ABN 41 687 119 230.

     This software may be distributed and modified according to the terms of
     the BSD 2-Clause license. Note that NO WARRANTY is provided.
     See "LICENSE_BSD2.txt" for details.

     @TAG(DATA61_BSD)
-->
libvirtqueue
-------------

**_This implementation is currently a work in progress_**

This directory contains a library implementation of a virtqueue inspired from
the virtio specification. This is intended to be used as a communication
mechanism between system components for bulk data transfer.
The goal of this implementation is to provide a generic interface for manipulating
and managing a 'virtqueue' connection. This library doesn't contain any code that
interfaces with seL4. It is expected that the user will provide
shared memory regions and notification/signalling handlers to this library.

To use this library in a project you can link `virtqueue` in your
target applications CMake configuration.

Caveats
--------

This library is still under active development. The current library implementation does not
correctly implement the API. Current shortcomings of the implementation include:
* The library only supports a single buffer (shared by both the used and available queue)
* The library does not share the buffer address between the two ends of the virtqueue. It is
  assumed all communication will occur over a single static shared buffer.
