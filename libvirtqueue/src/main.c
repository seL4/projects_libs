#include <stdlib.h>

#include <virtqueue.h>

#define VQ_LEN 2048

uint8_t avail[sizeof(uint16_t) * (2 + VQ_LEN)];
uint8_t used[2 * sizeof(uint16_t) + sizeof(vq_vring_used_elem_t) * VQ_LEN];

vq_vring_desc_t desc[VQ_LEN];

int main() {
    virtqueue_driver_t drv;
    virtqueue_device_t dev;

    virtqueue_init_driver(&drv, VQ_LEN, (void*) avail, (void*) used, desc, NULL, NULL);
}
