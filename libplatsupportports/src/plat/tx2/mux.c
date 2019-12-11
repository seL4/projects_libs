/*
 * Copyright 2015-2017, NVIDIA Corporation
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
 * This MUX driver is a port from NVIDIA's L4T tx2 pinctrl drivers.
 *
 * Parts of the function-to-pin map table has been taken from NVIDIA's L4T
 * kernel sources.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <utils/util.h>

#include <platsupport/io.h>
#include <platsupport/mux.h>
#include <platsupport/gpio.h>

#include <platsupportports/plat/gpio.h>
#include <platsupportports/plat/mux.h>

#define MUX_REG_SCHMT_SHIFT         (12)
#define MUX_REG_SF_SHIFT            (10)
#define MUX_REG_ENABLE_SHIFT        (6)
#define MUX_REG_OPEN_DRAIN_SHIFT    (5)
#define MUX_REG_TRISTATE_SHIFT      (4)
#define MUX_REG_PUPD_SHIFT          (2)
#define MUX_REG_PUPD_MASK           (0x3)
#define MUX_REG_SFIO_SELECT_SHIFT   (0)
#define MUX_REG_SFIO_SELECT_MASK    (0x3)

#define MUX_REG_SF_GPIO             (0)
#define MUX_REG_SF_HSIO             (1)

#define MUX_REG_PUPD_NONE           (0)
#define MUX_REG_PUPD_PULLDOWN       (1)
#define MUX_REG_PUPD_PULLUP         (2)

#define MUX_REG_TRISTATE_NORMAL     (0)
#define MUX_REG_TRISTATE_TRISTATE   (1)

/* Throughout this driver, the terms "pad" and "pin" are interchangeable.
 *
 * Each pin's input and output buffers are distinct and can be enabled
 * separately and operated concurrently (to support bidirectional signals).
 *
 * So we need to specify for each pin whether one, or both (or neither) of the
 * buffers needs to be enabled.
 *
 * Furthermore, each pin's output buffer can be driven by one of several output
 * drivers: push-pull, open-drain, high-z, weak-pullup, weak-pulldown.
 */
#define P_IN                            BIT(0)
#define P_PUSHPULL                      BIT(6)
#define P_BOTH                          (P_IN | P_PUSHPULL)
#define P_OPEN_DRAIN                    BIT(2)
#define P_TRISTATE                      BIT(3)
#define P_PULLUP                        BIT(4)
#define P_PULLDOWN                      BIT(5)
#define P_SCHMT                         BIT(6)

#define PINMAP_PINDESC(_gp, _mro, _msv, _in_out) \
    { \
        .gpio_pin = _gp, .mux_reg_offset = _mro, .mux_sfio_value = _msv, \
        .flags = _in_out \
    }

#define PINMAP_NULLDESC PINMAP_PINDESC(-1, -1, 0xF, 0)

#define PINMAP_1PIN(_n, _gp0, _mro0, _msv0, _io0) \
    [_n] = { \
        { \
            PINMAP_PINDESC(_gp0, _mro0, _msv0, _io0), \
            PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, \
            PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC \
        } \
    }

#define PINMAP_2PIN(_n, _gp0, _mro0, _msv0, _io0, _gp1, _mro1, _msv1, _io1) \
    [_n] = { \
        { \
            PINMAP_PINDESC(_gp0, _mro0, _msv0, _io0), \
            PINMAP_PINDESC(_gp1, _mro1, _msv1, _io1), \
            PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, \
            PINMAP_NULLDESC, PINMAP_NULLDESC \
        } \
    }

#define PINMAP_3PIN(_n, _gp0, _mro0, _msv0, _io0, _gp1, _mro1, _msv1, _io1, _gp2, _mro2, _msv2, _io2) \
    [_n] = { \
        { \
            PINMAP_PINDESC(_gp0, _mro0, _msv0, _io0), \
            PINMAP_PINDESC(_gp1, _mro1, _msv1, _io1), \
            PINMAP_PINDESC(_gp2, _mro2, _msv2, _io2), \
            PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, \
            PINMAP_NULLDESC \
        } \
    }

#define PINMAP_4PIN(_n, _gp0, _mro0, _msv0, _io0, _gp1, _mro1, _msv1, _io1, _gp2, _mro2, _msv2, _io2, _gp3, _mro3, _msv3, _io3) \
    [_n] = { \
        { \
            PINMAP_PINDESC(_gp0, _mro0, _msv0, _io0), \
            PINMAP_PINDESC(_gp1, _mro1, _msv1, _io1), \
            PINMAP_PINDESC(_gp2, _mro2, _msv2, _io2), \
            PINMAP_PINDESC(_gp3, _mro3, _msv3, _io3), \
            PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC, PINMAP_NULLDESC \
        } \
    }

#define PINMAP_6PIN(_n, _gp0, _mro0, _msv0, _io0, _gp1, _mro1, _msv1, _io1, _gp2, _mro2, _msv2, _io2, _gp3, _mro3, _msv3, _io3, _gp4, _mro4, _msv4, _io4, _gp5, _mro5, _msv5, _io5) \
    [_n] = { \
        { \
            PINMAP_PINDESC(_gp0, _mro0, _msv0, _io0), \
            PINMAP_PINDESC(_gp1, _mro1, _msv1, _io1), \
            PINMAP_PINDESC(_gp2, _mro2, _msv2, _io2), \
            PINMAP_PINDESC(_gp3, _mro3, _msv3, _io3), \
            PINMAP_PINDESC(_gp4, _mro4, _msv4, _io4), \
            PINMAP_PINDESC(_gp5, _mro5, _msv5, _io5), \
            PINMAP_NULLDESC, PINMAP_NULLDESC \
        } \
    }

typedef struct tx2_mux_feature_pinmap_ {
    struct tx2_mux_pin_desc {
        uint32_t    flags;
        int16_t     gpio_pin, mux_reg_offset;
        uint8_t     mux_sfio_value;
    } pins[8];
} tx2_mux_feature_pinmap_t;

tx2_mux_feature_pinmap_t pinmaps[NMUX_FEATURES] = {
    PINMAP_2PIN(MUX_FEATURE_UARTA, GPIO_PT0, MUX_PAD_UART1_TX_PT0, 0, P_PUSHPULL,
                GPIO_PT1, MUX_PAD_UART1_RX_PT1, 0, P_IN | P_PULLUP | P_TRISTATE),
    PINMAP_4PIN(MUX_FEATURE_UARTB, GPIO_PX0, MUX_PAD_UART2_TX_PX0, 0, P_PUSHPULL,
                GPIO_PX1, MUX_PAD_UART2_RX_PX1, 0, P_IN | P_PULLUP | P_TRISTATE,
                GPIO_PX2, MUX_PAD_UART2_RTS_PX2, 0, P_PUSHPULL,
                GPIO_PX3, MUX_PAD_UART2_CTS_PX3, 0, P_IN | P_PULLUP | P_TRISTATE),
    PINMAP_4PIN(MUX_FEATURE_UARTD, GPIO_PB0, MUX_PAD_UART4_TX_PB0, 0, P_PUSHPULL,
                GPIO_PB1, MUX_PAD_UART4_RX_PB1, 0, P_IN | P_PULLUP | P_TRISTATE,
                GPIO_PB2, MUX_PAD_UART4_RTS_PB2, 0, P_PUSHPULL,
                GPIO_PB3, MUX_PAD_UART4_CTS_PB3, 0, P_IN | P_PULLUP | P_TRISTATE),

    PINMAP_4PIN(MUX_FEATURE_SPI1, GPIO_PH0, MUX_PAD_GPIO_WAN5_PH0, 2, P_PUSHPULL,
                GPIO_PH1, MUX_PAD_GPIO_WAN6_PH1, 2, P_IN | P_PULLDOWN | P_TRISTATE,
                GPIO_PH2, MUX_PAD_GPIO_WAN7_PH2, 2, P_PUSHPULL,
                GPIO_PH3, MUX_PAD_GPIO_WAN8_PH3, 2, P_PULLUP),
    /* I can't find which SPI pin routes to which MUX pin, so both directions are enabled */
    PINMAP_4PIN(MUX_FEATURE_SPI3, GPIO_PX4, MUX_PAD_UART5_TX_PX4, 1, P_BOTH,
                GPIO_PX5, MUX_PAD_UART5_RX_PX5, 1, P_BOTH,
                GPIO_PX6, MUX_PAD_UART5_RTS_PX6, 1, P_BOTH,
                GPIO_PX7, MUX_PAD_UART5_CTS_PX7, 1, P_BOTH),
    PINMAP_4PIN(MUX_FEATURE_SPI4, GPIO_PN3, MUX_PAD_GPIO_CAM4_PN3, 1, P_PUSHPULL,
                GPIO_PN4, MUX_PAD_GPIO_CAM5_PN4, 1, P_IN | P_PULLDOWN | P_TRISTATE,
                GPIO_PN5, MUX_PAD_GPIO_CAM6_PN5, 1, P_PUSHPULL,
                GPIO_PN6, MUX_PAD_GPIO_CAM7_PN6, 1, P_PULLUP),

    PINMAP_2PIN(MUX_FEATURE_I2C1, GPIO_PC5, MUX_PAD_GEN1_I2C_SCL_PC5, 0, P_IN | P_OPEN_DRAIN,
                GPIO_PC6, MUX_PAD_GEN1_I2C_SDA_PC6, 0, P_IN | P_OPEN_DRAIN),
    PINMAP_2PIN(MUX_FEATURE_I2C3, GPIO_PO2, MUX_PAD_CAM_I2C_SCL_PO2, 0, P_IN,
                GPIO_PO3, MUX_PAD_CAM_I2C_SDA_PO3, 0, P_IN),
    PINMAP_2PIN(MUX_FEATURE_I2C7, GPIO_PL0, MUX_PAD_GEN7_I2C_SCL_PL0, 0, P_IN,
                GPIO_PL1, MUX_PAD_GEN7_I2C_SDA_PL1, 0, P_IN),
    PINMAP_2PIN(MUX_FEATURE_I2C9, GPIO_PL2, MUX_PAD_GEN9_I2C_SCL_PL2, 0, P_IN,
                GPIO_PL3, MUX_PAD_GEN9_I2C_SDA_PL3, 0, P_IN),

    PINMAP_6PIN(MUX_FEATURE_EQOS_RX, GPIO_PF3, MUX_PAD_EQOS_RXC_PF3, 0, P_IN | P_TRISTATE,
                GPIO_PE6, MUX_PAD_EQOS_RD0_PE6, 0, P_IN | P_TRISTATE,
                GPIO_PE7, MUX_PAD_EQOS_RD1_PE7, 0, P_IN | P_TRISTATE,
                GPIO_PF0, MUX_PAD_EQOS_RD2_PF0, 0, P_IN | P_TRISTATE,
                GPIO_PF1, MUX_PAD_EQOS_RD3_PF1, 0, P_IN | P_TRISTATE,
                GPIO_PF2, MUX_PAD_EQOS_RX_CTL_PF2, 0, P_IN | P_TRISTATE),

    PINMAP_6PIN(MUX_FEATURE_EQOS_TX, GPIO_PE0, MUX_PAD_EQOS_TXC_PE0, 0, P_PUSHPULL,
                GPIO_PE1, MUX_PAD_EQOS_TD0_PE1, 0, P_PUSHPULL,
                GPIO_PE2, MUX_PAD_EQOS_TD1_PE2, 0, P_PUSHPULL,
                GPIO_PE3, MUX_PAD_EQOS_TD2_PE3, 0, P_PUSHPULL,
                GPIO_PE4, MUX_PAD_EQOS_TD3_PE4, 0, P_PUSHPULL,
                GPIO_PE5, MUX_PAD_EQOS_TX_CTL_PE5, 0, P_PUSHPULL),

    PINMAP_2PIN(MUX_FEATURE_EQOS_MDIO, GPIO_PF4, MUX_PAD_EQOS_MDIO_PF4, 0, P_IN | P_PULLUP,
                GPIO_PF5, MUX_PAD_EQOS_MDC_PF5, 0, P_PUSHPULL),

    /* GPIOs don't require the SFIO function to be set properly. Just enable both buffers
     * and let the GPIO interface handle the rest of the configuration */
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ0, GPIO_PJ0, MUX_PAD_DAP1_SCLK_PJ0, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ1, GPIO_PJ1, MUX_PAD_DAP1_DOUT_PJ1, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ2, GPIO_PJ2, MUX_PAD_DAP1_DIN_PJ2, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ3, GPIO_PJ3, MUX_PAD_DAP1_FS_PJ3, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ4, GPIO_PJ4, MUX_PAD_AUD_MCLK_PJ4, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ5, GPIO_PJ5, MUX_PAD_GPIO_AUD0_PJ5, 0, P_BOTH),
    PINMAP_1PIN(MUX_FEATURE_GPIO_PJ6, GPIO_PJ6, MUX_PAD_GPIO_AUD1_PJ6, 0, P_BOTH),
};

enum mux_register_type {
    CONTROL_REGISTER,
    DRIVE_STRENGTH_REGISTER,
};

static bool is_valid_feature(enum mux_feature feature)
{
    return 0 <= feature && feature <= NMUX_FEATURES;
}

static inline volatile uint32_t *tx2_mux_get_register(void *mux_base, uint16_t mux_reg_offset,
                                                      enum mux_register_type reg_type)
{
    uintptr_t regs = (uintptr_t) mux_base;
    int offset = (reg_type == CONTROL_REGISTER ? 0 : 4);
    ZF_LOGD("Getting register type %d for pin with offset %hx, vaddr is = 0x%llx",
            reg_type, mux_reg_offset, regs + mux_reg_offset + offset);
    return (volatile uint32_t *)(regs + mux_reg_offset + offset);
}

static int tx2_mux_set_pin_params(mux_sys_t *mux, struct tx2_mux_pin_desc *desc, enum mux_gpio_dir dir)
{
    volatile uint32_t *pin_reg = tx2_mux_get_register(mux->priv, desc->mux_reg_offset, CONTROL_REGISTER);

    uint32_t regval = *pin_reg;

    /* Turn on tristate first whenever we set the pins parameters to avoid glitching. */
    *pin_reg = BIT(MUX_REG_TRISTATE_SHIFT);
    assert(*pin_reg == BIT(MUX_REG_TRISTATE_SHIFT));

    /* Or in the right SFIO function */
    uint32_t requiredval = (desc->mux_sfio_value & MUX_REG_SFIO_SELECT_MASK) << MUX_REG_SFIO_SELECT_MASK;

    ZF_LOGD("Setting params for pin with offset %hx", desc->mux_reg_offset);

    /* Enable input buffer if P_IN */
    if (desc->flags & P_IN) {
        requiredval |= BIT(MUX_REG_ENABLE_SHIFT);
    }
    /* Enable schmitt trigger if P_SCHMT */
    if (desc->flags & P_SCHMT) {
        requiredval |= BIT(MUX_REG_SCHMT_SHIFT);
    }
    /* Set output driver to pushpull if P_PUSHPULL. */
    if (desc->flags & P_PUSHPULL) {
        if (desc->flags & P_OPEN_DRAIN) {
            ZF_LOGE("Can't enable both pushpull and open-drain output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_TRISTATE) {
            ZF_LOGE("Can't enable both pushpull and tristate output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_PULLUP || desc->flags & P_PULLDOWN) {
            ZF_LOGE("Can't enable both pushpull and pull-resistor output drivers.");
            return -EINVAL;
        }
        requiredval &= ~BIT(MUX_REG_TRISTATE_SHIFT);
    }
    if (desc->flags & P_TRISTATE) {
        if (desc->flags & P_OPEN_DRAIN) {
            ZF_LOGE("Can't enable both tristate and open-drain output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_PUSHPULL) {
            ZF_LOGE("Can't enable both tristate and pushpull output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_PULLUP || desc->flags & P_PULLDOWN) {
            ZF_LOGE("Can't enable both tristate and pull-resistor output drivers.");
            return -EINVAL;
        }
        requiredval |= MUX_REG_TRISTATE_TRISTATE << MUX_REG_TRISTATE_SHIFT;
    }
    if (desc->flags & P_PULLUP || desc->flags & P_PULLDOWN) {
        if ((desc->flags & P_PULLUP) && (desc->flags & P_PULLDOWN)) {
            ZF_LOGE("Can't enable both pullup and pulldown at once.");
            return -EINVAL;
        }
        if (desc->flags & P_OPEN_DRAIN) {
            ZF_LOGE("Can't enable both pullup and opendrain output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_TRISTATE) {
            ZF_LOGE("Can't enable both pullup and tristate output drivers.");
            return -EINVAL;
        }
        requiredval &= ~(MUX_REG_PUPD_MASK << MUX_REG_PUPD_SHIFT);
        requiredval |= ((desc->flags & P_PULLUP)
                        ? MUX_REG_PUPD_PULLUP
                        : MUX_REG_PUPD_PULLDOWN)
                       << MUX_REG_PUPD_SHIFT;
    }
    if (desc->flags & P_OPEN_DRAIN) {
        if (desc->flags & P_PUSHPULL) {
            ZF_LOGE("Can't enable both open-drain and pushpull output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_TRISTATE) {
            ZF_LOGE("Can't enable both open-drain and tristate output drivers.");
            return -EINVAL;
        }
        if (desc->flags & P_PULLUP || desc->flags & P_PULLDOWN) {
            ZF_LOGE("Can't enable both open-drain and pullup/pulldown output drivers.");
            return -EINVAL;
        }
        requiredval |= BIT(MUX_REG_OPEN_DRAIN_SHIFT);
    }

    if (dir == MUX_DIR_NOT_A_GPIO) {
        requiredval |= BIT(MUX_REG_SFIO_SELECT_SHIFT);
    }

    if (regval == requiredval) {
        return 0;
    }

    *pin_reg = requiredval;
    ZF_LOGD("pin_reg = 0x%lx", *pin_reg);
    /* Just in case the writes are being silently ignored. */
    assert(*pin_reg == requiredval);
    return 0;

}

static int tx2_mux_feature_enable(mux_sys_t *mux, mux_feature_t feature, enum mux_gpio_dir dir)
{
    int error = 0;

    if (!is_valid_feature(feature)) {
        ZF_LOGE("Not a valid feature");
        return -EINVAL;
    }

    if (dir != MUX_DIR_NOT_A_GPIO) {
        if (!(MUX_FEATURE_GPIO_PJ0 <= feature && feature <= MUX_FEATURE_GPIO_PJ6)) {
            ZF_LOGE("Not a valid GPIO feature");
            return -EINVAL;
        }
    }

    ZF_LOGD("Enabling feature %d", feature);

    tx2_mux_feature_pinmap_t *map = &pinmaps[feature];

    for (int i = 0; i < ARRAY_SIZE(map->pins); i++) {
        if (map->pins[i].gpio_pin == -1) {
            /* The pin list is terminated by -1 */
            break;
        }

        error = tx2_mux_set_pin_params(mux, &map->pins[i], dir);
        if (error) {
            ZF_LOGE("Failed to set pinmux params for pin %d of feature %d", i, feature);
            return error;
        }
    }

    return 0;
}

static inline void tx2_mux_disable_pin(mux_sys_t *mux, struct tx2_mux_pin_desc *desc)
{
    /* 8.29.3 of the TRM:
     * For each unused MPIO, assert its tristate (TRISTATE_CONTROL) bit and
     * disable its input buffer (E_INPUT) bit. Disable the Pullup/Pull down control to
     * minimize the unnecessary power consumption on the unused pins.
     *
     * If all of the pins in a pad-control group are unused, set the drive strengths and slew rates to minimum.
     *
     * If all of the pins on a power rail are unused, assert E_NOIOPOWER for that rail in the PMC registers.
     */
    volatile uint32_t *pin_reg = tx2_mux_get_register(mux->priv, desc->mux_reg_offset, CONTROL_REGISTER);
    *pin_reg = 0 | (MUX_REG_TRISTATE_TRISTATE << MUX_REG_TRISTATE_SHIFT);
    ZF_LOGD("*pin_reg = 0x%lx", *pin_reg);
    /* Just in case the writes are being ignored */
    assert(*pin_reg == (MUX_REG_TRISTATE_TRISTATE << MUX_REG_TRISTATE_SHIFT));
}

static int tx2_mux_feature_disable(mux_sys_t *mux, mux_feature_t feature)
{
    int error = 0;

    if (!is_valid_feature(feature)) {
        return -EINVAL;
    }

    tx2_mux_feature_pinmap_t *map = &pinmaps[feature];
    for (int i = 0; i < ARRAY_SIZE(map->pins); i++) {
        if (map->pins[i].gpio_pin == -1) {
            /* The pin list is terminated by -1 */
            break;
        }

        tx2_mux_disable_pin(mux, &map->pins[i]);
    }

    return 0;
}

int mux_sys_init(ps_io_ops_t *io_ops, UNUSED void *dependencies, mux_sys_t *mux)
{
    void *pinmux_vaddr = NULL;

    pinmux_vaddr = ps_io_map(&io_ops->io_mapper, (uintptr_t) TX2_MUX_PADDR, TX2_MUX_SIZE, 0, PS_MEM_NORMAL);
    if (pinmux_vaddr == NULL) {
        ZF_LOGE("Failed to map in pinmux frames.");
        return -1;
    }

    ZF_LOGD("pinmux_vaddr = 0x%llx", pinmux_vaddr);

    mux->priv = pinmux_vaddr;
    mux->feature_enable = &tx2_mux_feature_enable;
    mux->feature_disable = &tx2_mux_feature_disable;

    return 0;
}
