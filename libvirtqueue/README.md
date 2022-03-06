<!--
    Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)

    SPDX-License-Identifier: CC-BY-SA-4.0
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

This library tries to follow the virtio standard. It currently offers used and
available rings, as well as a descriptor table, and two types of virtqueue, one
for the driver side and one for the device side.

The ring implementation and the descriptor table design follow closely the
[virtio paper](https://www.ozlabs.org/~rusty/virtio-spec/virtio-paper.pdf). The
API allows the user to create a new entry in the available ring, chain (scatter)
some buffers linked to that entry, transfer it into the used ring, and pop it.

Use case
----------

The driver wants to add some buffer that will be read by the device (for
instance to write something into a serial connection).
    1. The driver creates a new ring entry into the available ring and gets a
       handle back.
    2. The driver uses the handle to chain several buffers linked to that entry.
    3. The driver calls the notify callback to kick the device.
    4. The device gets the available ring entry, which comes down to getting the
       handle.
    5. The device uses the handle to pop all the buffers linked to it and read
       them.
    6. Finally, the device uses the handle to transfer the ring entry from the
       available ring to the used ring.
    7. The device notifies back the driver.
    8. The driver gets the handle to the used element and iterates through the
       buffers to free them.

ASCII art explanation
----------

Empty rings and desc table:

A [ ]<- first available entry                         [ ]
V [ ]                                                 [ ] D
A [ ]                                                 [ ] E
I [ ]                                                 [ ] S
L [ ]                                                 [ ] C
  [ ]                                                 [ ] R
R [ ]                                                 [ ] I
I [ ]                                                 [ ] P
N [ ]                                                 [ ] T
G [ ]                                                 [ ] O
                                                      [ ] R
                                                      [ ]
U [ ]<- first available entry                         [ ] T
S [ ]                                                 [ ] A
E [ ]                                                 [ ] B
D [ ]                                                 [ ] L
  [ ]                                                 [ ] E
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

Creating an available entry:

A [x]                                                 [ ]
V [ ]<- first available entry                         [ ] D
A [ ]                                                 [ ] E
I [ ]                                                 [ ] S
L [ ]                                                 [ ] C
  [ ]                                                 [ ] R
R [ ]                                                 [ ] I
I [ ]                                                 [ ] P
N [ ]                                                 [ ] T
G [ ]                                                 [ ] O
                                                      [ ] R
                                                      [ ]
U [ ]<- first available entry                         [ ] T
S [ ]                                                 [ ] A
E [ ]                                                 [ ] B
D [ ]                                                 [ ] L
  [ ]                                                 [ ] E
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

Adding one buffer linked to the entry:

A [x]------------------------------------------------>[x]
V [ ]<- first available entry                         [ ] D
A [ ]                                                 [ ] E
I [ ]                                                 [ ] S
L [ ]                                                 [ ] C
  [ ]                                                 [ ] R
R [ ]                                                 [ ] I
I [ ]                                                 [ ] P
N [ ]                                                 [ ] T
G [ ]                                                 [ ] O
                                                      [ ] R
                                                      [ ]
U [ ]<- first available entry                         [ ] T
S [ ]                                                 [ ] A
E [ ]                                                 [ ] B
D [ ]                                                 [ ] L
  [ ]                                                 [ ] E
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

Chaining more buffers:

A [x]------------------------------------------------>[x]-\
V [ ]<- first available entry                       /-[x]-/
A [ ]                                               \-[x]-\
I [ ]                                                 [ ] |
L [ ]                                                 [ ] |
  [ ]                                                 [ ] |
R [ ]                                                 [x]-/
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
                                                      [ ]
                                                      [ ]
U [ ]<- first available entry                         [ ]
S [ ]                                                 [ ]
E [ ]                                                 [ ]
D [ ]                                                 [ ]
  [ ]                                                 [ ]
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

The device pops the available entry and gets a handle which links to the head of
the list of buffers, through which it can iterate.

A [ ]                             handle ------------>[x]-\
V [ ]<- first available entry                       /-[x]-/
A [ ]                                               \-[x]-\
I [ ]                                                 [ ] |
L [ ]                                                 [ ] |
  [ ]                                                 [ ] |
R [ ]                                                 [x]-/
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
                                                      [ ]
                                                      [ ]
U [ ]<- first available entry                         [ ]
S [ ]                                                 [ ]
E [ ]                                                 [ ]
D [ ]                                                 [ ]
  [ ]                                                 [ ]
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

It pushes it back into the used ring.

A [ ]                                   /------------>[x]-\
V [ ]<- first available entry           |           /-[x]-/
A [ ]                                   |           \-[x]-\
I [ ]                                   |             [ ] |
L [ ]                                   |             [ ] |
  [ ]                                   |             [ ] |
R [ ]                                   |             [x]-/
I [ ]                                   |             [ ]
N [ ]                                   |             [ ]
G [ ]                                   |             [ ]
                                        |             [ ]
                                        |             [ ]
U [x]-----------------------------------/             [ ]
S [ ]<- first available entry                         [ ]
E [ ]                                                 [ ]
D [ ]                                                 [ ]
  [ ]                                                 [ ]
R [ ]                                                 [ ]
I [ ]                                                 [ ]
N [ ]                                                 [ ]
G [ ]                                                 [ ]
  [ ]                                                 [ ]

The driver side then pops the used entry and frees the buffers.
