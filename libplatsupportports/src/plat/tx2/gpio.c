/*
 * Copyright 2010-2016, NVIDIA Corporation
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

/*
 * This GPIO driver is a port of U-Boot's tx2 driver which has been
 * modified to fit into the interfaces that the platsupport GPIO interfaces
 * expose.
 */

#include <stddef.h>
#include <stdbool.h>

#include <utils/arith.h>
#include <utils/attribute.h>
#include <platsupport/gpio.h>

#include <platsupportports/plat/gpio.h>

#define TX2_GPIO_PIN_STRIDE 0x20

#define TX2_GPIO_ENABLE_CONFIG_ENABLE BIT(0)
#define TX2_GPIO_ENABLE_CONFIG_OUT BIT(1)
#define TX2_GPIO_ENABLE_CONFIG_INTERRUPT_ENABLE BIT(6)
#define TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL BIT(2)
#define TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE BIT(3)
#define TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_DOUBLE_EDGE BIT(2) | BIT (3)
#define TX2_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL_HIGH BIT(4)
#define TX2_GPIO_ENABLE_CONFIG_TRIGGER_MASK BIT(2) | BIT(3) | BIT(4)

#define TX2_GPIO_OUTPUT_CONTROL_FLOATED BIT(0)

#define TX2_GPIO_OUTPUT_VALUE_HIGH BIT(0)

#define TX2_GPIO_INTERRUPT_CLEAR BIT(0)

enum gpio_reg_offset {
    GPIO_ENABLE_CONFIG = 0x0,
    GPIO_INPUT = 0x8,
    GPIO_OUTPUT_CONTROL = 0xc,
    GPIO_OUTPUT_VALUE = 0x10,
    GPIO_INTERRUPT_CLEAR = 0x14,
    GPIO_INTERRUPT_STATUS = 0x100
};

struct tx2_gpio_port {
    uint32_t addr_offset;
    enum gpio_pin start;
    enum gpio_pin end;
};

/* This contains the offsets of the ports from the base of the controller */
static struct tx2_gpio_port tx2_ports[] = {
    [GPIO_PORT_A] = { .addr_offset = 0x2000, .start = GPIO_PA0, .end = GPIO_PA6},
    [GPIO_PORT_B] = { .addr_offset = 0x3000, .start = GPIO_PB0, .end = GPIO_PB6},
    [GPIO_PORT_C] = { .addr_offset = 0x3200, .start = GPIO_PC0, .end = GPIO_PC6},
    [GPIO_PORT_D] = { .addr_offset = 0x3400, .start = GPIO_PD0, .end = GPIO_PD5},
    [GPIO_PORT_E] = { .addr_offset = 0x2200, .start = GPIO_PE0, .end = GPIO_PE7},
    [GPIO_PORT_F] = { .addr_offset = 0x2400, .start = GPIO_PF0, .end = GPIO_PF5},
    [GPIO_PORT_G] = { .addr_offset = 0x4200, .start = GPIO_PG0, .end = GPIO_PG5},
    [GPIO_PORT_H] = { .addr_offset = 0x1000, .start = GPIO_PH0, .end = GPIO_PH6},
    [GPIO_PORT_I] = { .addr_offset = 0x800, .start = GPIO_PI0, .end = GPIO_PI7},
    [GPIO_PORT_J] = { .addr_offset = 0x5000, .start = GPIO_PJ0, .end = GPIO_PJ7},
    [GPIO_PORT_K] = { .addr_offset = 0x5200, .start = GPIO_PK0, .end = GPIO_PK0},
    [GPIO_PORT_L] = { .addr_offset = 0x1200, .start = GPIO_PL0, .end = GPIO_PL7},
    [GPIO_PORT_M] = { .addr_offset = 0x5600, .start = GPIO_PM0, .end = GPIO_PM5},
    [GPIO_PORT_N] = { .addr_offset = 0x0, .start = GPIO_PN0, .end = GPIO_PN6},
    [GPIO_PORT_O] = { .addr_offset = 0x200, .start = GPIO_PO0, .end = GPIO_PO3},
    [GPIO_PORT_P] = { .addr_offset = 0x4000, .start = GPIO_PP0, .end = GPIO_PP6},
    [GPIO_PORT_Q] = { .addr_offset = 0x400, .start = GPIO_PQ0, .end = GPIO_PQ5},
    [GPIO_PORT_R] = { .addr_offset = 0xa00, .start = GPIO_PR0, .end = GPIO_PR5},
    [GPIO_PORT_T] = { .addr_offset = 0x600, .start = GPIO_PT0, .end = GPIO_PT3},
    [GPIO_PORT_X] = { .addr_offset = 0x1400, .start = GPIO_PX0, .end = GPIO_PX7},
    [GPIO_PORT_Y] = { .addr_offset = 0x1600, .start = GPIO_PY0, .end = GPIO_PY6},
    [GPIO_PORT_BB] = { .addr_offset = 0x2600, .start = GPIO_PBB0, .end = GPIO_PBB1},
    [GPIO_PORT_CC] = { .addr_offset = 0x5400, .start = GPIO_PCC0, .end = GPIO_PCC3}
};

static inline enum gpio_port tx2_pin_to_port(gpio_id_t id)
{
    enum gpio_port port = id / 8;
    return port;
}

static bool tx2_valid_pin(gpio_id_t id)
{
    if (id < 0 || id > MAX_GPIO_ID) {
        return false;
    }

    enum gpio_port port = tx2_pin_to_port(id);

    return tx2_ports[port].start <= id && id <= tx2_ports[port].end;
}

static uint32_t *tx2_gpio_get_register(gpio_sys_t *gpio_sys, enum gpio_reg_offset reg_offset, enum gpio_pin pin)
{
    uintptr_t vaddr_base = (uintptr_t) gpio_sys->priv;
    enum gpio_port port = tx2_pin_to_port(pin);
    int pin_index = pin - tx2_ports[port].start;
    return (uint32_t *)(vaddr_base + tx2_ports[port].addr_offset + reg_offset + pin_index * TX2_GPIO_PIN_STRIDE);
}

static int tx2_gpio_set_direction(gpio_sys_t *gpio_sys, gpio_id_t gpio, enum gpio_dir dir)
{
    uint32_t config_val = 0;
    uint32_t output_control_val = 0;
    volatile uint32_t *config_reg = tx2_gpio_get_register(gpio_sys, GPIO_ENABLE_CONFIG, gpio);
    volatile uint32_t *output_control_reg = tx2_gpio_get_register(gpio_sys, GPIO_OUTPUT_CONTROL, gpio);

    config_val = *config_reg;
    output_control_val = *output_control_reg;

    switch (dir) {
    case GPIO_DIR_OUT_DEFAULT_HIGH:
    case GPIO_DIR_OUT_DEFAULT_LOW:
        config_val |= TX2_GPIO_ENABLE_CONFIG_OUT;
        output_control_val &= ~TX2_GPIO_OUTPUT_CONTROL_FLOATED;
        break;
    case GPIO_DIR_IN:
        config_val &= ~TX2_GPIO_ENABLE_CONFIG_OUT;
        output_control_val |= TX2_GPIO_OUTPUT_CONTROL_FLOATED;
    default:
        return -EINVAL;
    }

    *config_reg = config_val;
    *output_control_reg = output_control_val;

    return 0;
}

static int tx2_gpio_set_interrupt_type(gpio_sys_t *gpio_sys, enum gpio_pin gpio, enum gpio_dir dir)
{
    uint32_t val, lvl_type = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio_sys, GPIO_ENABLE_CONFIG, gpio);

    val = *reg_vaddr;
    switch (dir) {
    case GPIO_DIR_IRQ_RISE:
        lvl_type = TX2_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL_HIGH;
    case GPIO_DIR_IRQ_FALL:
        lvl_type |= TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE;
        break;
    case GPIO_DIR_IRQ_EDGE:
        lvl_type = TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_DOUBLE_EDGE;
        break;
    case GPIO_DIR_IRQ_HIGH:
        lvl_type = TX2_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL_HIGH;
    case GPIO_DIR_IRQ_LOW:
        lvl_type = TX2_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL;
        break;
    default:
        return -EINVAL;
    }

    val &= ~(TX2_GPIO_ENABLE_CONFIG_TRIGGER_MASK);
    val |= lvl_type;
    *reg_vaddr = val;

    return 0;
}

static int tx2_gpio_irq_enable_disable(gpio_t *gpio, bool enable)
{
    uint32_t val = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio->gpio_sys, GPIO_ENABLE_CONFIG, gpio->id);

    val = *reg_vaddr;
    if (enable) {
        val |= TX2_GPIO_ENABLE_CONFIG_INTERRUPT_ENABLE;
    } else {
        val &= ~(TX2_GPIO_ENABLE_CONFIG_INTERRUPT_ENABLE);
    }
    *reg_vaddr = val;

    return 0;
}

static int tx2_gpio_init(gpio_sys_t *gpio_sys, gpio_id_t id, enum gpio_dir dir, gpio_t *gpio)
{
    int error = 0;

    if (!tx2_valid_pin(id)) {
        return EINVAL;
    }

    gpio->id = id;
    gpio->gpio_sys = gpio_sys;

    /* Set the gpio enable bit */
    uint32_t val = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio_sys, GPIO_ENABLE_CONFIG, id);
    val = *reg_vaddr;
    val |= TX2_GPIO_ENABLE_CONFIG_ENABLE;
    *reg_vaddr = val;

    if (dir == GPIO_DIR_IN
        || dir == GPIO_DIR_OUT_DEFAULT_HIGH || dir == GPIO_DIR_OUT_DEFAULT_LOW) {
        error = tx2_gpio_set_direction(gpio_sys, id, dir);
        if (error) {
            return error;
        }
    } else {
        error = tx2_gpio_set_interrupt_type(gpio_sys, id, dir);
        if (error) {
            return error;
        }
        /* Leave the interrupt disabled and let the user enable it when they want */
        error = tx2_gpio_irq_enable_disable(gpio, false);
        if (error) {
            return error;
        }
    }

    return 0;
}

static int tx2_gpio_read_level(gpio_t *gpio)
{
    if (!tx2_valid_pin(gpio->id)) {
        return -EINVAL;
    }
    /* Maybe check if the pin is configured for output? */
    uint32_t val = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio->gpio_sys, GPIO_INPUT, gpio->id);
    val = *reg_vaddr;
    return (!!val) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static int tx2_gpio_set_level(gpio_t *gpio, enum gpio_level level)
{
    if (!tx2_valid_pin(gpio->id)) {
        return -EINVAL;
    }
    uint32_t val = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio->gpio_sys, GPIO_OUTPUT_VALUE, gpio->id);

    val = *reg_vaddr;
    if (GPIO_LEVEL_HIGH) {
        val |= TX2_GPIO_OUTPUT_VALUE_HIGH;
    } else {
        val &= ~(TX2_GPIO_OUTPUT_VALUE_HIGH);
    }

    *reg_vaddr = val;

    return 0;
}

static void tx2_gpio_int_clear(gpio_sys_t *gpio_sys, enum gpio_pin gpio)
{
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio_sys, GPIO_INTERRUPT_CLEAR, gpio);
    *reg_vaddr |= TX2_GPIO_INTERRUPT_CLEAR;
}

static bool tx2_gpio_check_pending(gpio_sys_t *gpio_sys, enum gpio_pin gpio)
{
    uint32_t val = 0;
    volatile uint32_t *reg_vaddr = tx2_gpio_get_register(gpio_sys, GPIO_INTERRUPT_STATUS, gpio);
    val = *reg_vaddr;
    return !!val;
}

static int tx2_gpio_pending_status(gpio_t *gpio, bool clear)
{
    int pending = 0;

    if (gpio == NULL) {
        return -EINVAL;
    }
    if (gpio->gpio_sys == NULL) {
        return -EINVAL;
    }

    pending = tx2_gpio_check_pending(gpio->gpio_sys, gpio->id);

    if (clear) {
        tx2_gpio_int_clear(gpio->gpio_sys, gpio->id);
    }

    return pending;
}

int gpio_sys_init(ps_io_ops_t *io_ops, gpio_sys_t *gpio_sys)
{
    if (io_ops == NULL) {
        return -EINVAL;
    }
    if (gpio_sys == NULL) {
        return -EINVAL;
    }

    /* TODO Do we need to clear the whole block?
     * Uboot sets up some registers for us and we may not want this.
     * If you get some output/input problems, this is probably it. */
    gpio_sys->priv = ps_io_map(&io_ops->io_mapper, (uintptr_t) TX2_GPIO_PADDR, TX2_GPIO_SIZE, 0, PS_MEM_NORMAL);
    if (gpio_sys->priv == NULL) {
        ZF_LOGE("Failed to map TX2 GPIO frame.");
        return -1;
    }

    gpio_sys->init = &tx2_gpio_init;
    gpio_sys->set_level = &tx2_gpio_set_level;
    gpio_sys->read_level = &tx2_gpio_read_level;
    gpio_sys->pending_status = &tx2_gpio_pending_status;
    gpio_sys->irq_enable_disable = &tx2_gpio_irq_enable_disable;

    return 0;
}
