/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2016, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/**
 * \file
 *
 * Implementation of memories configuration on board.
 *
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "board.h"
#include "board_timer.h"
#include "timer.h"
#include "trace.h"

#include "irq/irq.h"
#include "gpio/pio.h"
#include "peripherals/pmc.h"
#include "extram/smc.h"
#include "peripherals/wdt.h"

#include "extram/ddram.h"

#include "arm/mmu_cp15.h"
#include "mm/l1cache.h"

#include "board_support.h"

/*----------------------------------------------------------------------------
 *        Local constants
 *----------------------------------------------------------------------------*/

static const char* board_name = BOARD_NAME;

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/

SECTION(".region_ddr") ALIGNED(16384) static uint32_t tlb[4096];

/*----------------------------------------------------------------------------
 *        Local functions
 *----------------------------------------------------------------------------*/

static bool board_cfg_sd_dev_pins(uint32_t periph_id, bool down, bool up)
{
	struct _pin *dev_pins = NULL;
	uint32_t count = 0, pin;

#ifdef BOARD_HSMCI0_DEV_PINS
	struct _pin dev0_pins[] = BOARD_HSMCI0_DEV_PINS;
	if (periph_id == ID_HSMCI0) {
		dev_pins = dev0_pins;
		count = ARRAY_SIZE(dev0_pins);
	}
#endif

#ifdef BOARD_HSMCI1_DEV_PINS
	struct _pin dev1_pins[] = BOARD_HSMCI1_DEV_PINS;
	if (periph_id == ID_HSMCI1) {
		dev_pins = dev1_pins;
		count = ARRAY_SIZE(dev1_pins);
	}
#endif

	if (count == 0)
		return false;

	for (pin = 0; (down || up) && pin < count; pin++) {
		dev_pins[pin].type = up ? PIO_OUTPUT_1 : PIO_OUTPUT_0;
		dev_pins[pin].attribute = PIO_DEFAULT;
	}

	pio_configure(dev_pins, count);
	return true;
}

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

const char* get_board_name(void)
{
	return board_name;
}

void board_cfg_clocks(void)
{
	struct _pmc_plla_cfg plla_config = {
		.mul = 199,
		.div = 3,
		.count = 0x3f,
	};
	pmc_switch_mck_to_main();
	pmc_disable_plla();
	pmc_select_external_osc();
	pmc_configure_plla(&plla_config);
	pmc_set_mck_divider(PMC_MCKR_MDIV_PCK_DIV3);
	pmc_set_mck_plladiv2(true);
	pmc_set_mck_prescaler(PMC_MCKR_PRES_CLOCK);
	pmc_switch_mck_to_pll();
}

void board_cfg_lowlevel(bool clocks, bool ddram, bool mmu)
{
	/* Disable Watchdog */
	wdt_disable();

	/* Disable all PIO interrupts */
	pio_reset_all_it();

	/* Set the external oscillator frequency */
	pmc_set_main_oscillator_freq(BOARD_MAIN_CLOCK_EXT_OSC);

	if (clocks) {
		/* Configure system clocks */
		board_cfg_clocks();
	}

	/* Setup default interrupt handlers */
	irq_initialize();

	/* Configure system timer */
	board_cfg_timer();

	if (ddram) {
		/* Configure DDRAM */
		board_cfg_ddram();
	}

	if (mmu) {
		/* Setup MMU */
		board_cfg_mmu();
	}
}

void board_restore_pio_reset_state(void)
{
	int i;

	/* all pins */
	struct _pin pins[] = {
		{ PIO_GROUP_A, 0xFFFFFFFF, PIO_INPUT, PIO_PULLUP },
		{ PIO_GROUP_B, 0xFFFFFFFF, PIO_INPUT, PIO_PULLUP },
		{ PIO_GROUP_C, 0xFFFFFFFF, PIO_INPUT, PIO_PULLUP },
		{ PIO_GROUP_D, 0xFFFFFFFF, PIO_INPUT, PIO_PULLUP },
	};

	/* For low_power_mode example, power consumption results can be affected
	* by IOs setting. To generate power consumption numbers in datasheet,
	* most IOs must be disconnected from external devices just like on
	* VB board. Then putting IOs to reset state are OK.
	*/

	pio_configure(pins, ARRAY_SIZE(pins));
	for (i = 0; i < ARRAY_SIZE(pins); i++)
		pio_clear(&pins[i]);
}

void board_save_misc_power(void)
{
	int i;
	int tc_id = get_tc_id_from_addr(BOARD_TIMER_TC, BOARD_TIMER_CHANNEL);

	/* disable USB clock */
	pmc_disable_upll_clock();
	pmc_disable_upll_bias();

	/* disable system clocks */
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_DDR);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_LCD);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_SMD);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_UHP);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_UDP);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_PCK0);
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_PCK1);
#ifdef CONFIG_HAVE_ISC
	pmc_disable_system_clock(PMC_SYSTEM_CLOCK_ISC);
#endif

	/* disable all peripheral clocks */
	for (i = ID_PIOA; i < ID_PERIPH_COUNT; i++) {
		if (i == tc_id)
			continue;

		pmc_disable_peripheral(i);
	}
}

void board_cfg_mmu(void)
{
	uint32_t addr;

	if (mmu_is_enabled())
		return;

	/* TODO: some peripherals are configured TTB_SECT_STRONGLY_ORDERED
	   instead of TTB_SECT_SHAREABLE_DEVICE because their drivers have to
	   be verified for correct operation when write-back is enabled */

	/* Reset table entries */
	for (addr = 0; addr < 4096; addr++)
		tlb[addr] = 0;

	/* 0x00000000: SRAM (Remapped) */
	tlb[0x000] = TTB_SECT_ADDR(0x00000000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_CACHEABLE_WB
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0x00100000: ROM */
	tlb[0x001] = TTB_SECT_ADDR(0x00100000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_CACHEABLE_WB
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0x00300000: SRAM */
	tlb[0x003] = TTB_SECT_ADDR(0x00300000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_CACHEABLE_WB
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0x00400000: SMD */
	tlb[0x004] = TTB_SECT_ADDR(0x00400000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_SHAREABLE_DEVICE
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

#ifdef CONFIG_HAVE_UDPHS
	/* 0x00500000: UDPHS RAM */
	tlb[0x005] = TTB_SECT_ADDR(0x00500000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_SHAREABLE_DEVICE
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0x00600000: UHP (OHCI) */
	tlb[0x006] = TTB_SECT_ADDR(0x00600000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_SHAREABLE_DEVICE
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0x00700000: UHP (EHCI) */
	tlb[0x007] = TTB_SECT_ADDR(0x00700000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_SHAREABLE_DEVICE
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;
#endif /* CONFIG_HAVE_UDPHS */

	/* 0x10000000: EBI Chip Select 0 */
	for (addr = 0x100; addr < 0x200; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0x20000000: EBI Chip Select 1 / DDR CS */
	/* (64MB cacheable, 192MB strongly ordered) */
	for (addr = 0x200; addr < 0x240; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_CACHEABLE_WB
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;
	for (addr = 0x240; addr < 0x300; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0x30000000: EBI Chip Select 2 */
	for (addr = 0x300; addr < 0x400; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0x40000000: EBI Chip Select 3 */
	for (addr = 0x400; addr < 0x500; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0x50000000: EBI Chip Select 4 */
	for (addr = 0x500; addr < 0x600; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0x60000000: EBI Chip Select 5 */
	for (addr = 0x600; addr < 0x700; addr++)
		tlb[addr] = TTB_SECT_ADDR(addr << 20)
	                  | TTB_SECT_AP_FULL_ACCESS
	                  | TTB_SECT_DOMAIN(0xf)
	                  | TTB_SECT_STRONGLY_ORDERED
	                  | TTB_SECT_SBO
	                  | TTB_TYPE_SECT;

	/* 0xf0000000: Peripherals */
	tlb[0xf00] = TTB_SECT_ADDR(0xf0000000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_STRONGLY_ORDERED
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0xf8000000: Peripherals */
	tlb[0xf80] = TTB_SECT_ADDR(0xf8000000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_STRONGLY_ORDERED
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* 0xfff0000: System Controller */
	tlb[0xfff] = TTB_SECT_ADDR(0xfff00000)
	           | TTB_SECT_AP_FULL_ACCESS
	           | TTB_SECT_DOMAIN(0xf)
	           | TTB_SECT_STRONGLY_ORDERED
	           | TTB_SECT_SBO
	           | TTB_TYPE_SECT;

	/* Enable MMU, I-Cache and D-Cache */
	mmu_configure(tlb);
	icache_enable();
	mmu_enable();
	dcache_enable();
}

void board_cfg_matrix_for_ddr(void)
{
	uint32_t csa = MATRIX->MATRIX_EBICSA;
	csa |= MATRIX_EBICSA_EBI_CS1A_DDRC;
	csa &= ~MATRIX_EBICSA_EBI_DBPUC;
	csa |= MATRIX_EBICSA_EBI_DBPDC;
	csa &= ~MATRIX_EBICSA_EBI_DRIVE;
	MATRIX->MATRIX_EBICSA = csa;
}

void board_cfg_matrix_for_nand(void)
{
	uint32_t csa = MATRIX->MATRIX_EBICSA;
	csa |= MATRIX_EBICSA_EBI_CS3A_NAND;
	csa |= MATRIX_EBICSA_NFD0_ON_D16;
	csa &= ~MATRIX_EBICSA_EBI_DRIVE;
	MATRIX->MATRIX_EBICSA = csa;
}

void board_cfg_ddram(void)
{
#ifdef BOARD_DDRAM_TYPE
	board_cfg_matrix_for_ddr();
	struct _mpddrc_desc desc;
	ddram_init_descriptor(&desc, BOARD_DDRAM_TYPE);
	ddram_configure(&desc);
#endif
}

#ifdef CONFIG_HAVE_NAND_FLASH
void board_cfg_nand_flash(void)
{
#if defined(BOARD_NANDFLASH_PINS) && defined(BOARD_NANDFLASH_BUS_WIDTH)
	board_cfg_matrix_for_nand();
	const struct _pin pins_nandflash[] = BOARD_NANDFLASH_PINS;
	pio_configure(pins_nandflash, ARRAY_SIZE(pins_nandflash));
	smc_nand_configure(BOARD_NANDFLASH_BUS_WIDTH);
#else
	trace_fatal("Cannot configure NAND: target board have no NAND definitions!");
#endif
}
#endif /* CONFIG_HAVE_NAND_FLASH */


bool board_cfg_sdmmc(uint32_t periph_id)
{
	switch (periph_id) {
	case ID_HSMCI0:
	{
#ifdef BOARD_HSMCI0_PINS
		const struct _pin pins[] = BOARD_HSMCI0_PINS;

		/* Configure HSMCI0 pins */
		pio_configure(pins, ARRAY_SIZE(pins));
		return true;
#else
		trace_fatal("Target board misses HSMCI0 pins");
		return false;
#endif
	}
	case ID_HSMCI1:
	{
#ifdef BOARD_HSMCI1_PINS
		const struct _pin pins[] = BOARD_HSMCI1_PINS;

		/* Configure HSMCI1 pins */
		pio_configure(pins, ARRAY_SIZE(pins));
		return true;
#else
		trace_fatal("Target board misses HSMCI1 pins");
		return false;
#endif
	}
	default:
		return false;
	}
}

bool board_get_hsmci_card_detect_status(uint32_t periph_id)
{
	const struct _pin *cd_input = NULL;

#ifdef BOARD_HSMCI0_PIN_CD
	const struct _pin cd0_input = BOARD_HSMCI0_PIN_CD;
	cd_input = periph_id == ID_HSMCI0 ? &cd0_input : cd_input;
#endif

#ifdef BOARD_HSMCI1_PIN_CD
	const struct _pin cd1_input = BOARD_HSMCI1_PIN_CD;
	cd_input = periph_id == ID_HSMCI1 ? &cd1_input : cd_input;
#endif

	if (periph_id != ID_HSMCI0 && periph_id != ID_HSMCI1)
		return false;

	/* no detection, assume card is always present */
	if (!cd_input)
		return true;

	return pio_get(cd_input) ? false : true;
}

bool board_set_hsmci_card_power(uint32_t periph_id, bool on)
{
	const struct _pin *pwr_ctrl = NULL;

#ifdef BOARD_HSMCI0_PIN_POWER
	const struct _pin pwr0_ctrl = BOARD_HSMCI0_PIN_POWER;
	pwr_ctrl = periph_id == ID_HSMCI0 ? &pwr0_ctrl : pwr_ctrl;
#endif

#ifdef BOARD_HSMCI1_PIN_POWER
	const struct _pin pwr1_ctrl = BOARD_HSMCI1_PIN_POWER;
	pwr_ctrl = periph_id == ID_HSMCI1 ? &pwr1_ctrl : pwr_ctrl;
#endif

	if (periph_id != ID_HSMCI0 && periph_id != ID_HSMCI1)
		return false;

	/* This slot doesn't support switching VDD on/off */
	if (!pwr_ctrl)
		return false;

	if (on) {
		/*
		 * Workaround HW issue: flipping straight the VDD switch often
		 * causes the VCC_3V3 rail to drop and trigger reset upon
		 * under-voltage.
		 */
		board_cfg_sd_dev_pins(periph_id, false, true);
		msleep(100);
		pio_clear(pwr_ctrl);
		/* Wait for the VDD rail to settle at nominal voltage */
		msleep(1);
		board_cfg_sd_dev_pins(periph_id, false, false);
		msleep(1);
	} else {
		pio_set(pwr_ctrl);
		/*
		 * Drive all device signals low, in an attempt to have VDD
		 * falling quicker.
		 */
		board_cfg_sd_dev_pins(periph_id, true, false);
	}

	return true;
}
