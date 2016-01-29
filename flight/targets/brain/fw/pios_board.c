/**
 ******************************************************************************
 * @addtogroup TauLabsTargets Tau Labs Targets
 * @{
 * @addtogroup FlyingF4 FlyingF4 support files
 * @{
 *
 * @file       pios_board.c
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2012-2013
 * @author     dRonin, http://dronin.org Copyright (C) 2015
 * @brief      The board specific initialization routines
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* Pull in the board-specific static HW definitions.
 * Including .c files is a bit ugly but this allows all of
 * the HW definitions to be const and static to limit their
 * scope.
 *
 * NOTE: THIS IS THE ONLY PLACE THAT SHOULD EVER INCLUDE THIS FILE
 */

#include "board_hw_defs.c"

#include <pios.h>
#include <pios_hal.h>
#include <openpilot.h>
#include <uavobjectsinit.h>
#include "hwbrain.h"
#include "modulesettings.h"
#include "manualcontrolsettings.h"
#include "onscreendisplaysettings.h"

/* The ADDRESSES of the _bu_payload_* symbols */
extern const uint32_t _bu_payload_start;
extern const uint32_t _bu_payload_end;
extern const uint32_t _bu_payload_size;

/**
 * Configuration for the MPU9250 chip
 */
#if defined(PIOS_INCLUDE_MPU9250_BRAIN)
#include "pios_mpu9250_brain.h"
static const struct pios_exti_cfg pios_exti_mpu9250_cfg __exti_config = {
	.vector = PIOS_MPU9250_IRQHandler,
	.line = EXTI_Line13,
	.pin = {
		.gpio = GPIOC,
		.init = {
			.GPIO_Pin = GPIO_Pin_13,
			.GPIO_Speed = GPIO_Speed_2MHz,  // XXXX
			.GPIO_Mode = GPIO_Mode_IN,
			.GPIO_OType = GPIO_OType_OD,
			.GPIO_PuPd = GPIO_PuPd_NOPULL,
		},
	},
	.irq = {
		.init = {
			.NVIC_IRQChannel = EXTI15_10_IRQn,
			.NVIC_IRQChannelPreemptionPriority = PIOS_IRQ_PRIO_MID,
			.NVIC_IRQChannelSubPriority = 0,
			.NVIC_IRQChannelCmd = ENABLE,
		},
	},
	.exti = {
		.init = {
			.EXTI_Line = EXTI_Line13, // matches above GPIO pin
			.EXTI_Mode = EXTI_Mode_Interrupt,
			.EXTI_Trigger = EXTI_Trigger_Rising,
			.EXTI_LineCmd = ENABLE,
		},
	},
};

static const struct pios_mpu9250_cfg pios_mpu9250_cfg = {
	.exti_cfg = &pios_exti_mpu9250_cfg,
	.default_samplerate = 500,  // Note: has to be generated by dividing 1kHz
	.interrupt_cfg = PIOS_MPU60X0_INT_CLR_ANYRD,
	.interrupt_en = PIOS_MPU60X0_INTEN_DATA_RDY,
	.User_ctl = 0,
	.Pwr_mgmt_clk = PIOS_MPU60X0_PWRMGMT_PLL_Z_CLK,
	.orientation = PIOS_MPU60X0_TOP_0DEG
};
#endif /* PIOS_INCLUDE_MPU9250 */

#if defined(PIOS_INCLUDE_HMC5883)
#include "pios_hmc5883_priv.h"

static const struct pios_hmc5883_cfg pios_hmc5883_external_cfg = {
	.M_ODR = PIOS_HMC5883_ODR_75,
	.Meas_Conf = PIOS_HMC5883_MEASCONF_NORMAL,
	.Gain = PIOS_HMC5883_GAIN_1_9,
	.Mode = PIOS_HMC5883_MODE_SINGLE,
	.Default_Orientation = PIOS_HMC5883_TOP_0DEG,
};
#endif /* PIOS_INCLUDE_HMC5883 */

#if defined(PIOS_INCLUDE_HMC5983_I2C)
#include "pios_hmc5983.h"

static const struct pios_hmc5983_cfg pios_hmc5983_external_cfg = {
	.M_ODR = PIOS_HMC5983_ODR_75,
	.Meas_Conf = PIOS_HMC5983_MEASCONF_NORMAL,
	.Gain = PIOS_HMC5983_GAIN_1_9,
	.Mode = PIOS_HMC5983_MODE_SINGLE,
	.Orientation = PIOS_HMC5983_TOP_0DEG,
};
#endif /* PIOS_INCLUDE_HMC5983 */

/**
 * Configuration for the MS5611 chip
 */
#if defined(PIOS_INCLUDE_MS5611)
#include "pios_ms5611_priv.h"
static const struct pios_ms5611_cfg pios_ms5611_cfg = {
	.oversampling = MS5611_OSR_4096,
	.temperature_interleaving = 1,
};
#endif /* PIOS_INCLUDE_MS5611 */

#if defined(PIOS_INCLUDE_FRSKY_RSSI)
#include "pios_frsky_rssi_priv.h"
#endif /* PIOS_INCLUDE_FRSKY_RSSI */

/* One slot per selectable receiver group.
 *  eg. PWM, PPM, GCS, SPEKTRUM1, SPEKTRUM2, SBUS
 * NOTE: No slot in this map for NONE.
 */
bool external_mag_fail;

uintptr_t pios_internal_adc_id;
uintptr_t pios_com_logging_id;

uintptr_t pios_uavo_settings_fs_id;
uintptr_t pios_waypoints_settings_fs_id;
uintptr_t streamfs_id;

/**
* Initialise PWM Output for black/white level setting
*/

#if defined(PIOS_INCLUDE_VIDEO)
void OSD_configure_bw_levels(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_OCInitTypeDef  TIM_OCInitStructure;
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;

	/* --------------------------- System Clocks Configuration -----------------*/
	/* TIM1 clock enable */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

	/* GPIOA clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	/* Connect TIM1 pins to AF */
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_TIM1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_TIM1);

	/*-------------------------- GPIO Configuration ----------------------------*/
	GPIO_StructInit(&GPIO_InitStructure); // Reset init structure
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);


	/* Time base configuration */
	TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
	TIM_TimeBaseStructure.TIM_Prescaler = (PIOS_SYSCLK / 25500000) - 1; // Get clock to 25 MHz on STM32F2/F4
	TIM_TimeBaseStructure.TIM_Period = 255;
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

	/* Enable TIM1 Preload register on ARR */
	TIM_ARRPreloadConfig(TIM1, ENABLE);

	/* TIM PWM1 Mode configuration */
	TIM_OCStructInit(&TIM_OCInitStructure);
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse = 90;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;

	/* Output Compare PWM1 Mode configuration: Channel1 PA.08 */
	TIM_OC1Init(TIM1, &TIM_OCInitStructure);
	TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
	TIM_OC3Init(TIM1, &TIM_OCInitStructure);
	TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable);

	/* TIM1 Main Output Enable */
	TIM_CtrlPWMOutputs(TIM1, ENABLE);

	/* TIM1 enable counter */
	TIM_Cmd(TIM1, ENABLE);
	TIM1->CCR1 = 30;
	TIM1->CCR3 = 110;
}
#endif /* PIOS_INCLUDE_VIDEO */

/**
 * Indicate a target-specific error code when a component fails to initialize
 * 1 pulse - flash chip
 * 2 pulses - MPU6050
 * 3 pulses - HMC5883
 * 4 pulses - MS5611
 * 5 pulses - gyro I2C bus locked
 * 6 pulses - mag/baro I2C bus locked
 */
void panic(int32_t code) {
    PIOS_HAL_Panic(PIOS_LED_ALARM, code);
}

/**
 * PIOS_Board_Init()
 * initializes all the core subsystems on this specific hardware
 * called from System/openpilot.c
 */

#include <pios_board_info.h>

void PIOS_Board_Init(void) {
	bool use_internal_mag = true;
	bool ext_mag_init_ok = false;
	bool use_rxport_usart = false;

	/* Delay system */
	PIOS_DELAY_Init();

#if defined(PIOS_INCLUDE_LED)
	PIOS_LED_Init(&pios_led_cfg);
#endif	/* PIOS_INCLUDE_LED */

#if defined(PIOS_INCLUDE_I2C)
	if (PIOS_I2C_Init(&pios_i2c_internal_id, &pios_i2c_internal_cfg)) {
		PIOS_DEBUG_Assert(0);
	}
	if (PIOS_I2C_CheckClear(pios_i2c_internal_id) != 0)
		panic(3);
#endif

#if defined(PIOS_INCLUDE_SPI)
	if (PIOS_SPI_Init(&pios_spi_flash_id, &pios_spi_flash_cfg)) {
		PIOS_DEBUG_Assert(0);
	}
#endif

#if defined(PIOS_INCLUDE_FLASH)
	/* Inititialize all flash drivers */
	if (PIOS_Flash_Internal_Init(&pios_internal_flash_id, &flash_internal_cfg) != 0)
		panic(1);
	if (PIOS_Flash_Jedec_Init(&pios_external_flash_id, pios_spi_flash_id, 0, &flash_mx25_cfg) != 0)
		panic(1);

	/* Register the partition table */
	PIOS_FLASH_register_partition_table(pios_flash_partition_table, NELEMENTS(pios_flash_partition_table));

	/* Mount all filesystems */
	if (PIOS_FLASHFS_Logfs_Init(&pios_uavo_settings_fs_id, &flashfs_settings_cfg, FLASH_PARTITION_LABEL_SETTINGS) != 0)
		panic(1);
	if (PIOS_FLASHFS_Logfs_Init(&pios_waypoints_settings_fs_id, &flashfs_waypoints_cfg, FLASH_PARTITION_LABEL_WAYPOINTS) != 0)
		panic(1);

#if defined(EMBED_BL_UPDATE)
	/* Update the bootloader if necessary */

	uintptr_t bl_partition_id;
	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_BL, &bl_partition_id) != 0){
		panic(2);
	}

	uint32_t bl_crc32 = PIOS_CRC32_updateCRC(0, (uint8_t *)0x08000000, _bu_payload_size);
	uint32_t bl_new_crc32 = PIOS_CRC32_updateCRC(0, (uint8_t *)&_bu_payload_start, _bu_payload_size);

	if (bl_new_crc32 != bl_crc32 ){
		/* The bootloader needs to be updated */

		/* Erase the partition */
		PIOS_LED_On(PIOS_LED_ALARM);
		PIOS_FLASH_start_transaction(bl_partition_id);
		PIOS_FLASH_erase_partition(bl_partition_id);
		PIOS_FLASH_end_transaction(bl_partition_id);

		/* Write in the new bootloader */
		PIOS_FLASH_start_transaction(bl_partition_id);
		PIOS_FLASH_write_data(bl_partition_id, 0, (uint8_t *)&_bu_payload_start, _bu_payload_size);
		PIOS_FLASH_end_transaction(bl_partition_id);
		PIOS_LED_Off(PIOS_LED_ALARM);

		/* Blink the LED to indicate BL update */
		for (uint8_t i=0; i<10; i++){
			PIOS_DELAY_WaitmS(50);
			PIOS_LED_On(PIOS_LED_ALARM);
			PIOS_DELAY_WaitmS(50);
			PIOS_LED_Off(PIOS_LED_ALARM);
		}
	}
#endif /* EMBED_BL_UPDATE */

#endif	/* PIOS_INCLUDE_FLASH */

	RCC_ClearFlag(); // The flags cleared after use

	/* Initialize UAVObject libraries */
	UAVObjInitialize();

	HwBrainInitialize();
	ModuleSettingsInitialize();

#if defined(PIOS_INCLUDE_RTC)
	/* Initialize the real-time clock and its associated tick */
	PIOS_RTC_Init(&pios_rtc_main_cfg);
#endif

	/* Initialize watchdog as early as possible to catch faults during init
	 * but do it only if there is no debugger connected
	 */
	if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) == 0) {
		PIOS_WDG_Init();
	}

	/* Initialize the alarms library */
	AlarmsInitialize();

	/* Initialize the task monitor library */
	TaskMonitorInitialize();

	/* Set up pulse timers */
	//inputs
	PIOS_TIM_InitClock(&tim_8_cfg);
	PIOS_TIM_InitClock(&tim_12_cfg);
	//outputs
	PIOS_TIM_InitClock(&tim_5_cfg);

	/* IAP System Setup */
	PIOS_IAP_Init();
	uint16_t boot_count = PIOS_IAP_ReadBootCount();
	if (boot_count < 3) {
		PIOS_IAP_WriteBootCount(++boot_count);
		AlarmsClear(SYSTEMALARMS_ALARM_BOOTFAULT);
	} else {
		/* Too many failed boot attempts, force hw config to defaults */
		HwBrainSetDefaults(HwBrainHandle(), 0);
		ModuleSettingsSetDefaults(ModuleSettingsHandle(),0);
		AlarmsSet(SYSTEMALARMS_ALARM_BOOTFAULT, SYSTEMALARMS_ALARM_CRITICAL);
	}

#if defined(PIOS_INCLUDE_USB)
	/* Initialize board specific USB data */
	PIOS_USB_BOARD_DATA_Init();

	/* Flags to determine if various USB interfaces are advertised */
	bool usb_hid_present = false;
	bool usb_cdc_present = false;

#if defined(PIOS_INCLUDE_USB_CDC)
	if (PIOS_USB_DESC_HID_CDC_Init()) {
		PIOS_Assert(0);
	}
	usb_hid_present = true;
	usb_cdc_present = true;
#else
	if (PIOS_USB_DESC_HID_ONLY_Init()) {
		PIOS_Assert(0);
	}
	usb_hid_present = true;
#endif

	uintptr_t pios_usb_id;
	PIOS_USB_Init(&pios_usb_id, &pios_usb_main_cfg);

#if defined(PIOS_INCLUDE_USB_CDC)

	uint8_t hw_usb_vcpport;
	/* Configure the USB VCP port */
	HwBrainUSB_VCPPortGet(&hw_usb_vcpport);

	if (!usb_cdc_present) {
		/* Force VCP port function to disabled if we haven't advertised VCP in our USB descriptor */
		hw_usb_vcpport = HWBRAIN_USB_VCPPORT_DISABLED;
	}

	PIOS_HAL_ConfigureCDC(hw_usb_vcpport, pios_usb_id, &pios_usb_cdc_cfg);
	
#endif	/* PIOS_INCLUDE_USB_CDC */

#if defined(PIOS_INCLUDE_USB_HID)
	/* Configure the usb HID port */
	uint8_t hw_usb_hidport;
	HwBrainUSB_HIDPortGet(&hw_usb_hidport);

	if (!usb_hid_present) {
		/* Force HID port function to disabled if we haven't advertised HID in our USB descriptor */
		hw_usb_hidport = HWBRAIN_USB_HIDPORT_DISABLED;
	}

	PIOS_HAL_ConfigureHID(hw_usb_hidport, pios_usb_id, &pios_usb_hid_cfg);
	
#endif	/* PIOS_INCLUDE_USB_HID */

	if (usb_hid_present || usb_cdc_present) {
		PIOS_USBHOOK_Activate();
	}
#endif	/* PIOS_INCLUDE_USB */

	/* Configure the IO ports */
	HwBrainDSMxModeOptions hw_DSMxMode;
	HwBrainDSMxModeGet(&hw_DSMxMode);

	/* Main Port */
	uint8_t hw_mainport;
	HwBrainMainPortGet(&hw_mainport);

	PIOS_HAL_ConfigurePort(hw_mainport,          // port type protocol
			&pios_mainport_cfg,                  // usart_port_cfg
			&pios_mainport_cfg,                  // frsky usart_port_cfg
			&pios_usart_com_driver,              // com_driver
			NULL,                                // i2c_id
			NULL,                                // i2c_cfg
			NULL,                                // ppm_cfg
			NULL,                                // pwm_cfg
			PIOS_LED_ALARM,                      // led_id
			&pios_mainport_dsm_hsum_cfg,         // usart_dsm_hsum_cfg
			&pios_mainport_dsm_aux_cfg,          // dsm_cfg
			hw_DSMxMode,                         // dsm_mode
			&pios_mainport_sbus_cfg,             // sbus_rcvr_cfg
			&pios_mainport_sbus_aux_cfg,         // sbus_cfg
			true);                               // sbus_toggle

	if (hw_mainport != HWBRAIN_MAINPORT_SBUS) {
		GPIO_Init(pios_mainport_sbus_aux_cfg.inv.gpio, (GPIO_InitTypeDef*)&pios_mainport_sbus_aux_cfg.inv.init);
		GPIO_WriteBit(pios_mainport_sbus_aux_cfg.inv.gpio, pios_mainport_sbus_aux_cfg.inv.init.GPIO_Pin, pios_mainport_sbus_aux_cfg.gpio_inv_disable);
	}

	/* Flx Port */
	uint8_t hw_flxport;
	HwBrainFlxPortGet(&hw_flxport);

	PIOS_HAL_ConfigurePort(hw_flxport,           // port type protocol
			&pios_flxport_cfg,                   // usart_port_cfg
			&pios_flxport_cfg,                   // frsky usart_port_cfg
			&pios_usart_com_driver,              // com_driver
			&pios_i2c_flexi_id,                  // i2c_id
			&pios_i2c_flexi_cfg,                 // i2c_cfg
			NULL,                                // ppm_cfg
			NULL,                                // pwm_cfg
			PIOS_LED_ALARM,                      // led_id
			&pios_flxport_dsm_hsum_cfg,          // usart_dsm_hsum_cfg
			&pios_flxport_dsm_aux_cfg,           // dsm_cfg
			hw_DSMxMode,                         // dsm_mode
			NULL,                                // sbus_rcvr_cfg
			NULL,                                // sbus_cfg
			false);                              // sbus_toggle

	/* Configure the rcvr port */
	uint8_t hw_rxport;
	HwBrainRxPortGet(&hw_rxport);

	switch (hw_rxport) {
	case HWBRAIN_RXPORT_DISABLED:
		break;

	case HWBRAIN_RXPORT_PWM:
		PIOS_HAL_ConfigurePort(HWSHARED_PORTTYPES_PWM,  // port type protocol
				NULL,                                   // usart_port_cfg
				NULL,                                   // frsky usart_port_cfg
				NULL,                                   // com_driver
				NULL,                                   // i2c_id
				NULL,                                   // i2c_cfg
				NULL,                                   // ppm_cfg
				&pios_pwm_cfg,                          // pwm_cfg
				PIOS_LED_ALARM,                         // led_id
				NULL,                                   // usart_dsm_hsum_cfg
				NULL,                                   // dsm_cfg
				0,                                      // dsm_mode
				NULL,                                   // sbus_rcvr_cfg
				NULL,                                   // sbus_cfg    
				false);                                 // sbus_toggle
		break;

	case HWBRAIN_RXPORT_PPMFRSKY:
		// special mode that enables PPM, FrSky RSSI, and Sensor Hub
		PIOS_HAL_ConfigurePort(HWSHARED_PORTTYPES_FRSKYSENSORHUB,  // port type protocol
				NULL,                                              // usart_port_cfg
				&pios_rxportusart_cfg,                             // frsky usart_port_cfg
				NULL,                                              // com_driver
				NULL,                                              // i2c_id
				NULL,                                              // i2c_cfg
				NULL,                                              // ppm_cfg
				NULL,                                              // pwm_cfg
				PIOS_LED_ALARM,                                    // led_id
				NULL,                                              // usart_dsm_hsum_cfg
				NULL,                                              // dsm_cfg
				0,                                                 // dsm_mode
				NULL,                                              // sbus_rcvr_cfg
				NULL,                                              // sbus_cfg    
				false);                                            // sbus_toggle
	
	case HWBRAIN_RXPORT_PPM:
	case HWBRAIN_RXPORT_PPMOUTPUTS:
		PIOS_HAL_ConfigurePort(HWSHARED_PORTTYPES_PPM,  // port type protocol
				NULL,                                   // usart_port_cfg
				NULL,                                   // frsky usart_port_cfg
				NULL,                                   // com_driver
				NULL,                                   // i2c_id
				NULL,                                   // i2c_cfg
				&pios_ppm_cfg,                          // ppm_cfg
				NULL,                                   // pwm_cfg
				PIOS_LED_ALARM,                         // led_id
				NULL,                                   // usart_dsm_hsum_cfg
				NULL,                                   // dsm_cfg
				0,                                      // dsm_mode
				NULL,                                   // sbus_rcvr_cfg
				NULL,                                   // sbus_cfg    
				false);                                 // sbus_toggle
		break;

	case HWBRAIN_RXPORT_PPMUART:
	case HWBRAIN_RXPORT_PPMUARTOUTPUTS:
		use_rxport_usart = true;

		PIOS_HAL_ConfigurePort(HWSHARED_PORTTYPES_PPM,  // port type protocol
				NULL,                                   // usart_port_cfg
				NULL,                                   // frsky usart_port_cfg
				NULL,                                   // com_driver
				NULL,                                   // i2c_id
				NULL,                                   // i2c_cfg
				&pios_ppm_cfg,                          // ppm_cfg
				NULL,                                   // pwm_cfg
				PIOS_LED_ALARM,                         // led_id
				NULL,                                   // usart_dsm_hsum_cfg
				NULL,                                   // dsm_cfg
				0,                                      // dsm_mode
				NULL,                                   // sbus_rcvr_cfg
				NULL,                                   // sbus_cfg    
				false);                                 // sbus_toggle
		break;
	}

	/* Configure the RxPort USART */
	if (use_rxport_usart) {
		uint8_t hw_rxportusart;
		HwBrainRxPortUsartGet(&hw_rxportusart);

		PIOS_HAL_ConfigurePort(hw_rxportusart,       // port type protocol
				&pios_rxportusart_cfg,               // usart_port_cfg
				&pios_rxportusart_cfg,               // frsky usart_port_cfg
				&pios_usart_com_driver,              // com_driver
				NULL,                                // i2c_id
				NULL,                                // i2c_cfg
				NULL,                                // ppm_cfg
				NULL,                                // pwm_cfg
				PIOS_LED_ALARM,                      // led_id
				&pios_rxportusart_dsm_hsum_cfg,      // usart_dsm_hsum_cfg
				&pios_rxportusart_dsm_aux_cfg,       // dsm_cfg
				hw_DSMxMode,                         // dsm_mode
				NULL,                                // sbus_rcvr_cfg
				NULL,                                // sbus_cfg
				false);                              // sbus_toggle
	}

#if defined(PIOS_INCLUDE_GCSRCVR)
	GCSReceiverInitialize();
	uintptr_t pios_gcsrcvr_id;
	PIOS_GCSRCVR_Init(&pios_gcsrcvr_id);
	uintptr_t pios_gcsrcvr_rcvr_id;
	if (PIOS_RCVR_Init(&pios_gcsrcvr_rcvr_id, &pios_gcsrcvr_rcvr_driver, pios_gcsrcvr_id)) {
		PIOS_Assert(0);
	}
	pios_rcvr_group_map[MANUALCONTROLSETTINGS_CHANNELGROUPS_GCS] = pios_gcsrcvr_rcvr_id;
#endif	/* PIOS_INCLUDE_GCSRCVR */

#ifndef PIOS_DEBUG_ENABLE_DEBUG_PINS
	switch (hw_rxport) {
	case HWBRAIN_RXPORT_DISABLED:
	case HWBRAIN_RXPORT_PWM:
	case HWBRAIN_RXPORT_PPM:
	case HWBRAIN_RXPORT_UART:
	case HWBRAIN_RXPORT_PPMUART:
	/* Set up the servo outputs */
#ifdef PIOS_INCLUDE_SERVO
		PIOS_Servo_Init(&pios_servo_cfg);
#endif
		break;
	case HWBRAIN_RXPORT_PPMOUTPUTS:
#ifdef PIOS_INCLUDE_SERVO
		PIOS_Servo_Init(&pios_servo_rcvr_ppm_cfg);
#endif
		break;
	case HWBRAIN_RXPORT_PPMUARTOUTPUTS:
#ifdef PIOS_INCLUDE_SERVO
		PIOS_Servo_Init(&pios_servo_rcvr_ppm_uart_out_cfg);
#endif
		break;
	case HWBRAIN_RXPORT_PPMFRSKY:
#ifdef PIOS_INCLUDE_SERVO
		PIOS_Servo_Init(&pios_servo_cfg);
#endif
#if defined(PIOS_INCLUDE_FRSKY_RSSI)
		PIOS_FrSkyRssi_Init(&pios_frsky_rssi_cfg);
#endif /* PIOS_INCLUDE_FRSKY_RSSI */
		break;
	case HWBRAIN_RXPORT_OUTPUTS:
#ifdef PIOS_INCLUDE_SERVO
		PIOS_Servo_Init(&pios_servo_rcvr_all_cfg);
#endif
		break;
	}
#else
	PIOS_DEBUG_Init(&pios_tim_servo_all_channels, NELEMENTS(pios_tim_servo_all_channels));
#endif

#if defined(PIOS_INCLUDE_GPIO)
	PIOS_GPIO_Init();
#endif

	/* init sensor queue registration */
	PIOS_SENSORS_Init();

	//I2C is slow, sensor init as well, reset watchdog to prevent reset here
	PIOS_WDG_Clear();

#if defined(PIOS_INCLUDE_MS5611)
	PIOS_MS5611_Init(&pios_ms5611_cfg, pios_i2c_internal_id);
	if (PIOS_MS5611_Test() != 0)
		panic(4);
#endif

	PIOS_WDG_Clear();

	/* Magnetometer selection */
	uint8_t Magnetometer;
	HwBrainMagnetometerGet(&Magnetometer);
	switch (Magnetometer) {
		case HWBRAIN_MAGNETOMETER_DISABLED:
			use_internal_mag = false;
			ext_mag_init_ok = true;
			break;
		case HWBRAIN_MAGNETOMETER_FLXPORTHMC5883:
			if (hw_flxport == HWBRAIN_FLXPORT_I2C){
#if defined(PIOS_INCLUDE_HMC5883)
				if (PIOS_HMC5883_Init(pios_i2c_flexi_id, &pios_hmc5883_external_cfg) == 0) {
					if (PIOS_HMC5883_Test() == 0) {
						// sensor configuration was successful: external mag is attached and powered

						// setup sensor orientation
						uint8_t ExtMagOrientation;
						HwBrainExtMagOrientationGet(&ExtMagOrientation);

						enum pios_hmc5883_orientation hmc5883_orientation = \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP0DEGCW) ? PIOS_HMC5883_TOP_0DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP90DEGCW) ? PIOS_HMC5883_TOP_90DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP180DEGCW) ? PIOS_HMC5883_TOP_180DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP270DEGCW) ? PIOS_HMC5883_TOP_270DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM0DEGCW) ? PIOS_HMC5883_BOTTOM_0DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM90DEGCW) ? PIOS_HMC5883_BOTTOM_90DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM180DEGCW) ? PIOS_HMC5883_BOTTOM_180DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM270DEGCW) ? PIOS_HMC5883_BOTTOM_270DEG : \
							pios_hmc5883_external_cfg.Default_Orientation;
						PIOS_HMC5883_SetOrientation(hmc5883_orientation);
						ext_mag_init_ok = true;
					}
				}
			}
#endif /* PIOS_INCLUDE_HMC5883 */
			use_internal_mag = false;
			break;
		case HWBRAIN_MAGNETOMETER_FLXPORTHMC5983:
			if (hw_flxport == HWBRAIN_FLXPORT_I2C){
#if defined(PIOS_INCLUDE_HMC5983_I2C)
				if (PIOS_HMC5983_Init(pios_i2c_flexi_id, 0, &pios_hmc5983_external_cfg) == 0) {

					if (PIOS_HMC5983_Test() == 0) {
						// sensor configuration was successful: external mag is attached and powered

						// setup sensor orientation
						uint8_t ExtMagOrientation;
						HwBrainExtMagOrientationGet(&ExtMagOrientation);

						enum pios_hmc5983_orientation hmc5983_orientation = \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP0DEGCW) ? PIOS_HMC5983_TOP_0DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP90DEGCW) ? PIOS_HMC5983_TOP_90DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP180DEGCW) ? PIOS_HMC5983_TOP_180DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_TOP270DEGCW) ? PIOS_HMC5983_TOP_270DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM0DEGCW) ? PIOS_HMC5983_BOTTOM_0DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM90DEGCW) ? PIOS_HMC5983_BOTTOM_90DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM180DEGCW) ? PIOS_HMC5983_BOTTOM_180DEG : \
							(ExtMagOrientation == HWBRAIN_EXTMAGORIENTATION_BOTTOM270DEGCW) ? PIOS_HMC5983_BOTTOM_270DEG : \
							pios_hmc5983_external_cfg.Orientation;
						PIOS_HMC5983_SetOrientation(hmc5983_orientation);
						ext_mag_init_ok = true;
					}
				}
			}
#endif /* PIOS_INCLUDE_HMC5983_I2C */
			use_internal_mag = false;
			break;
	}

#if defined(PIOS_INCLUDE_MPU9250_BRAIN)
#if defined(PIOS_INCLUDE_MPU6050)
	// Enable autoprobing when both 6050 and 9050 compiled in
	bool mpu9250_found = false;
	if (PIOS_MPU9250_Probe(pios_i2c_internal_id, PIOS_MPU9250_I2C_ADD_A0_LOW) == 0) {
		mpu9250_found = true;
#else
	{
#endif /* PIOS_INCLUDE_MPU6050 */
		uint8_t hw_mpu9250_dlpf;
		HwBrainMPU9250GyroLPFGet(&hw_mpu9250_dlpf);
		enum pios_mpu9250_gyro_filter mpu9250_gyro_lpf = \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_184) ? PIOS_MPU9250_GYRO_LOWPASS_184_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_92) ? PIOS_MPU9250_GYRO_LOWPASS_92_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_41) ? PIOS_MPU9250_GYRO_LOWPASS_41_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_20) ? PIOS_MPU9250_GYRO_LOWPASS_20_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_10) ? PIOS_MPU9250_GYRO_LOWPASS_10_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250GYROLPF_5) ? PIOS_MPU9250_GYRO_LOWPASS_5_HZ : \
			PIOS_MPU9250_GYRO_LOWPASS_184_HZ;

		HwBrainMPU9250AccelLPFGet(&hw_mpu9250_dlpf);
		enum pios_mpu9250_accel_filter mpu9250_accel_lpf = \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_460) ? PIOS_MPU9250_ACCEL_LOWPASS_460_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_184) ? PIOS_MPU9250_ACCEL_LOWPASS_184_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_92) ? PIOS_MPU9250_ACCEL_LOWPASS_92_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_41) ? PIOS_MPU9250_ACCEL_LOWPASS_41_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_20) ? PIOS_MPU9250_ACCEL_LOWPASS_20_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_10) ? PIOS_MPU9250_ACCEL_LOWPASS_10_HZ : \
			(hw_mpu9250_dlpf == HWBRAIN_MPU9250ACCELLPF_5) ? PIOS_MPU9250_ACCEL_LOWPASS_5_HZ : \
			PIOS_MPU9250_ACCEL_LOWPASS_184_HZ;

		int retval;
		retval = PIOS_MPU9250_Init(pios_i2c_internal_id, PIOS_MPU9250_I2C_ADD_A0_LOW, use_internal_mag,
									mpu9250_gyro_lpf, mpu9250_accel_lpf, &pios_mpu9250_cfg);
		if (retval == -10)
			panic(1); // indicate missing IRQ separately
		if (retval != 0)
			panic(2);

		// To be safe map from UAVO enum to driver enum
		uint8_t hw_gyro_range;
		HwBrainGyroFullScaleGet(&hw_gyro_range);
		switch(hw_gyro_range) {
			case HWBRAIN_GYROFULLSCALE_250:
				PIOS_MPU9250_SetGyroRange(PIOS_MPU60X0_SCALE_250_DEG);
				break;
			case HWBRAIN_GYROFULLSCALE_500:
				PIOS_MPU9250_SetGyroRange(PIOS_MPU60X0_SCALE_500_DEG);
				break;
			case HWBRAIN_GYROFULLSCALE_1000:
				PIOS_MPU9250_SetGyroRange(PIOS_MPU60X0_SCALE_1000_DEG);
				break;
			case HWBRAIN_GYROFULLSCALE_2000:
				PIOS_MPU9250_SetGyroRange(PIOS_MPU60X0_SCALE_2000_DEG);
				break;
		}

		uint8_t hw_accel_range;
		HwBrainAccelFullScaleGet(&hw_accel_range);
		switch(hw_accel_range) {
			case HWBRAIN_ACCELFULLSCALE_2G:
				PIOS_MPU9250_SetAccelRange(PIOS_MPU60X0_ACCEL_2G);
				break;
			case HWBRAIN_ACCELFULLSCALE_4G:
				PIOS_MPU9250_SetAccelRange(PIOS_MPU60X0_ACCEL_4G);
				break;
			case HWBRAIN_ACCELFULLSCALE_8G:
				PIOS_MPU9250_SetAccelRange(PIOS_MPU60X0_ACCEL_8G);
				break;
			case HWBRAIN_ACCELFULLSCALE_16G:
				PIOS_MPU9250_SetAccelRange(PIOS_MPU60X0_ACCEL_16G);
				break;
		}

		uint8_t mpu_rate;
		HwBrainMPU9250RateGet(&mpu_rate);
		switch(mpu_rate) {
			case HWBRAIN_MPU9250RATE_200:
				PIOS_MPU9250_SetSampleRate(200);
				break;
			case HWBRAIN_MPU9250RATE_250:
				PIOS_MPU9250_SetSampleRate(250);
				break;
			case HWBRAIN_MPU9250RATE_333:
				PIOS_MPU9250_SetSampleRate(333);
				break;
			case HWBRAIN_MPU9250RATE_500:
				PIOS_MPU9250_SetSampleRate(500);
				break;
			case HWBRAIN_MPU9250RATE_1000:
				PIOS_MPU9250_SetSampleRate(1000);
				break;
		}
	}
#endif /* PIOS_INCLUDE_MPU9250_BRAIN */

	PIOS_WDG_Clear();

#if defined(PIOS_INCLUDE_ADC)
	uint32_t internal_adc_id;
	PIOS_INTERNAL_ADC_Init(&internal_adc_id, &pios_adc_cfg);
	PIOS_ADC_Init(&pios_internal_adc_id, &pios_internal_adc_driver, internal_adc_id);
#endif

#if defined(PIOS_INCLUDE_FLASH)
	if ( PIOS_STREAMFS_Init(&streamfs_id, &streamfs_settings, FLASH_PARTITION_LABEL_LOG) != 0)
		panic(8);

	const uint32_t LOG_BUF_LEN = 256;
	uint8_t *log_rx_buffer = PIOS_malloc(LOG_BUF_LEN);
	uint8_t *log_tx_buffer = PIOS_malloc(LOG_BUF_LEN);
	if (PIOS_COM_Init(&pios_com_logging_id, &pios_streamfs_com_driver, streamfs_id,
					  log_rx_buffer, LOG_BUF_LEN, log_tx_buffer, LOG_BUF_LEN) != 0)
		panic(9);
#endif /* PIOS_INCLUDE_FLASH */

#if defined(PIOS_INCLUDE_VIDEO)
	// make sure the mask pin is low
	GPIO_Init(pios_video_cfg.mask.miso.gpio, (GPIO_InitTypeDef*)&pios_video_cfg.mask.miso.init);
	GPIO_ResetBits(pios_video_cfg.mask.miso.gpio, pios_video_cfg.mask.miso.init.GPIO_Pin);

	// Initialize settings
	OnScreenDisplaySettingsInitialize();

	uint8_t osd_state;
	OnScreenDisplaySettingsOSDEnabledGet(&osd_state);
	if (osd_state == ONSCREENDISPLAYSETTINGS_OSDENABLED_ENABLED) {
		OSD_configure_bw_levels();
	}
#endif

	// set variable so the Sensors task sets an alarm
	external_mag_fail = !use_internal_mag && !ext_mag_init_ok;

	/* Make sure we have at least one telemetry link configured or else fail initialization */
	PIOS_Assert(pios_com_telem_serial_id || pios_com_telem_usb_id);
}

/**
 * @}
 */
