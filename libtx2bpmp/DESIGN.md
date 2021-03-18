<!--
    Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)

    SPDX-License-Identifier: CC-BY-SA-4.0
-->

libtx2bpmp
==========

This library contains drivers necessary to interface with the Boot and Power
Management Processor (BPMP) co-processor on the NVIDIA TX2. The BPMP is
responsible for handling boot and power management functions and also controls
a few other devices. These devices are the clock controller, and an I2C
channel.

The child devices of the BPMP are managed by sending requests to the BPMP
module via inter-processor communication mechanisms which involve the Inter-VM
Communication (IVC) protocol, Hardware Synchronisation Primitives (HSP) device
module, and some shared memory.

HSP
---

The HSP module offers a number of primitives that can be used to synchronise
state between the processor and the co-processors on the board. These include:
    - Shared mailboxes, which are essentially small data transfer buffers
    - Shared semaphores
    - Arbitrated sempahores, which are essentially semaphores that also include
      state to avoid busy waiting
    - Doorbells, which are essentially signals 

The HSP driver in this library currently only supports the doorbell mechanisms.
This driver is mainly used only by the BPMP driver in this library, but it can
be used for other purposes as well.

Doorbells can be setup with the CPU complex/CPU subsystem (CCPLEX), Sensor
Processing Engine (SPE), Safety Cluster Engine (SCE), and the Audio Processing
Engine (APE) device modules. To first setup a doorbell, `tx2_hsp_init()` needs
to be called first to initialise the HSP interfaces. Afterwards, the doorbells
can to the device modules can be rung or checked by calling
`tx2_hsp_doorbell_ring()` or `tx2_hsp_doorbell_check()` respectively with the
right doorbell ID.

IVC
---

The IVC protocol is used to facilitate communication over a shared memory
channel between the processor and other remote entities. The protocol functions
similar to the producer-consumer pattern. The channel is split into fixed size
message buffers that can be pushed or popped from the channel when messages
need to be sent or received.
