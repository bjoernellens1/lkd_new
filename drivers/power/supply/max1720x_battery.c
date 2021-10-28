/*
 * Copyright (C) 2019 Maxim Integrated Products, Inc.
 * Author : Maxim Integrated <opensource@maximintegrated.com>
 *
 * This program is free software; you can redistribute  it and/or modify
 * it under  the terms of  the GNU General  Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define DRV_NAME "max1720x"

/* CONFIG register bits */
#define MAX1720X_CONFIG_ALRT_EN (1 << 2)
/* CONFIG register bits for MAX17330 */
#define MAX17330_CONFIG_SHIP (1 << 7)

/* CONFIG2 register bits */
#define MAX1720X_CONFIG2_POR_CMD (1 << 15)

/* STATUS register bits */
#define MAX1720X_STATUS_BST (1 << 3)
#define MAX1720X_STATUS_POR (1 << 1)

/* STATUS interrupt status bits */
#define MAX1720X_STATUS_ALRT_CLR_MASK (0x08BB)
#define MAX17320_STATUS_ALRT_CLR_MASK (0x88BB)
#define MAX17330_STATUS_ALRT_CLR_MASK (0x807F)
#define MAX1720X_STATUS_PROTECTION_ALRT (1 << 15)
#define MAX1720X_STATUS_BR_ALRT (1 << 15)
#define MAX1720X_STATUS_SOC_MAX_ALRT (1 << 14)
#define MAX1720X_STATUS_TEMP_MAX_ALRT (1 << 13)
#define MAX1720X_STATUS_VOLT_MAX_ALRT (1 << 12)
#define MAX1720X_STATUS_CHARGING_ALRT (1 << 11)
#define MAX1720X_STATUS_SOC_MIN_ALRT (1 << 10)
#define MAX1720X_STATUS_TEMP_MIN_ALRT (1 << 9)
#define MAX1720X_STATUS_VOLT_MIN_ALRT (1 << 8)
#define MAX1720X_STATUS_CURR_MAX_ALRT (1 << 6)
#define MAX1720X_STATUS_CURR_MIN_ALRT (1 << 2)

/* ProtStatus register bits for MAX1730X */
#define MAX1730X_PROTSTATUS_CHGWDT (1 << 15)
#define MAX1730X_PROTSTATUS_TOOHOTC (1 << 14)
#define MAX1730X_PROTSTATUS_FULL (1 << 13)
#define MAX1730X_PROTSTATUS_TOOCOLDC (1 << 12)
#define MAX1730X_PROTSTATUS_OVP (1 << 11)
#define MAX1730X_PROTSTATUS_OCCP (1 << 10)
#define MAX1730X_PROTSTATUS_QOVFLW (1 << 9)
#define MAX1730X_PROTSTATUS_RESCFAULT (1 << 7)
#define MAX1730X_PROTSTATUS_PERMFAIL (1 << 6)
#define MAX1730X_PROTSTATUS_DIEHOT (1 << 5)
#define MAX1730X_PROTSTATUS_TOOHOTD (1 << 4)
#define MAX1730X_PROTSTATUS_UVP (1 << 3)
#define MAX1730X_PROTSTATUS_ODCP (1 << 2)
#define MAX1730X_PROTSTATUS_RESDFAULT (1 << 1)
#define MAX1730X_PROTSTATUS_SHDN (1 << 0)

/* ProtStatus register bits for MAX17320 */
#define MAX17320_PROTSTATUS_CHGWDT (1 << 15)
#define MAX17320_PROTSTATUS_TOOHOTC (1 << 14)
#define MAX17320_PROTSTATUS_FULL (1 << 13)
#define MAX17320_PROTSTATUS_TOOCOLDC (1 << 12)
#define MAX17320_PROTSTATUS_OVP (1 << 11)
#define MAX17320_PROTSTATUS_OCCP (1 << 10)
#define MAX17320_PROTSTATUS_QOVFLW (1 << 9)
#define MAX17320_PROTSTATUS_PREQF (1 << 8)
#define MAX17320_PROTSTATUS_IMBALANCE (1 << 7)
#define MAX17320_PROTSTATUS_PERMFAIL (1 << 6)
#define MAX17320_PROTSTATUS_DIEHOT (1 << 5)
#define MAX17320_PROTSTATUS_TOOHOTD (1 << 4)
#define MAX17320_PROTSTATUS_UVP (1 << 3)
#define MAX17320_PROTSTATUS_ODCP (1 << 2)
#define MAX17320_PROTSTATUS_RESDFAULT (1 << 1)
#define MAX17320_PROTSTATUS_SHDN (1 << 0)

/* ProtStatus register bits for MAX17330 */
#define MAX17330_PROTSTATUS_CHGWDT (1 << 15)
#define MAX17330_PROTSTATUS_TOOHOTC (1 << 14)
#define MAX17330_PROTSTATUS_FULL (1 << 13)
#define MAX17330_PROTSTATUS_TOOCOLDC (1 << 12)
#define MAX17330_PROTSTATUS_OVP (1 << 11)
#define MAX17330_PROTSTATUS_OCCP (1 << 10)
#define MAX17330_PROTSTATUS_QOVFLW (1 << 9)
#define MAX17330_PROTSTATUS_PREQF (1 << 8)
#define MAX17330_PROTSTATUS_BLOCKCHG (1 << 7)
#define MAX17330_PROTSTATUS_PERMFAIL (1 << 6)
#define MAX17330_PROTSTATUS_DIEHOT (1 << 5)
#define MAX17330_PROTSTATUS_TOOHOTD (1 << 4)
#define MAX17330_PROTSTATUS_UVP (1 << 3)
#define MAX17330_PROTSTATUS_ODCP (1 << 2)
#define MAX17330_PROTSTATUS_TOOCOLDD (1 << 1)
#define MAX17330_PROTSTATUS_SHDN (1 << 0)

/*ChgStat register bits for MAX17330 */
#define MAX17330_CHGSTAT_DROPOUT (1 << 15)
#define MAX17330_CHGSTAT_CP (1 << 3)
#define MAX17330_CHGSTAT_CT (1 << 2)
#define MAX17330_CHGSTAT_CC (1 << 1)
#define MAX17330_CHGSTAT_CV (1 << 0)

/* CommStat register bits for MAX17330 */
#define MAX17330_COMMSTAT_CHGOFF (1 << 8)
#define MAX17330_COMMSTAT_DISOFF (1 << 9)

/* nConfig register bits for MAX17330 */
#define MAX17330_NCONFIG_PBEN_POS 10
#define MAX17330_NCONFIG_PBEN (1 << MAX17330_NCONFIG_PBEN_POS)

/* nIChgCfg register bits for MAX17330 */
#define MAX17330_NJEITAC_ROOMCHARGINGI_POS 8
#define MAX17330_NJEITAC_ROOMCHARGINGI (0xFF << MAX17330_NJEITAC_ROOMCHARGINGI_POS)

/* nVChgCfg register bits for MAX17330 */
#define MAX17330_NJEITAV_ROOMCHARGINGV_POS 8
#define MAX17330_NJEITAV_ROOMCHARGINGV (0xFF << MAX17330_NJEITAV_ROOMCHARGINGV_POS)

/* nChgCfg0 register bits for MAX17330 */
#define MAX17330_NCHGCFG0_PRECHGCURR_POS 0
#define MAX17330_NCHGCFG0_PRECHGCURR (0x1F << MAX17330_NCHGCFG0_PRECHGCURR_POS)
#define MAX17330_NCHGCFG0_VSYSMIN_POS 5
#define MAX17330_NCHGCFG0_VSYSMIN (0x7 << MAX17330_NCHGCFG0_VSYSMIN_POS)
#define MAX17330_NCHGCFG0_PREQUALVOLT_POS 8
#define MAX17330_NCHGCFG0_PREQUALVOLT (0x1F << MAX17330_NCHGCFG0_PREQUALVOLT_POS)

/* nChgCfg1 register bits for MAX17330 */
#define MAX17330_NCHGCFG1_FETTHETA_POS 0
#define MAX17330_NCHGCFG1_FETTHETA (0x1F << MAX17330_NCHGCFG1_FETTHETA_POS)
#define MAX17330_NCHGCFG1_FETLIM_POS 5
#define MAX17330_NCHGCFG1_FETLIM (0xF << MAX17330_NCHGCFG1_FETLIM_POS)
#define MAX17330_NCHGCFG1_HEATLIM_POS 9
#define MAX17330_NCHGCFG1_HEATLIM (0x1F << MAX17330_NCHGCFG1_HEATLIM_POS)

/* nVEmpty register bits for MAX17330 */
#define MAX17330_NVEMPTY_VR_POS 0
#define MAX17330_NVEMPTY_VR (0x7F << MAX17330_NVEMPTY_VR_POS)
#define MAX17330_NVEMPTY_VE_POS 7
#define MAX17330_NVEMPTY_VE (0x1FF << MAX17330_NVEMPTY_VE_POS)

/* nOVPrtTh register bits for MAX17330 */
#define MAX17330_NOVPRTTH_DOVPR_POS 0
#define MAX17330_NOVPRTTH_DOVPR (0xF << MAX17330_NOVPRTTH_DOVPR_POS)
#define MAX17330_NOVPRTTH_DOVP_POS 4
#define MAX17330_NOVPRTTH_DOVP (0xF << MAX17330_NOVPRTTH_DOVP_POS)
#define MAX17330_NOVPRTTH_CHGDETTH_POS 8
#define MAX17330_NOVPRTTH_CHGDETTH (0x7 << MAX17330_NOVPRTTH_CHGDETTH_POS)
#define MAX17330_NOVPRTTH_OVPPERMFAIL_POS 12
#define MAX17330_NOVPRTTH_OVPPERMFAIL (0xF << MAX17330_NOVPRTTH_OVPPERMFAIL_POS)

/* nIPrtTh1 register bits for MAX17330 */
#define MAX17330_NIPRTTH1_ODCP_POS 0
#define MAX17330_NIPRTTH1_ODCP (0xFF << MAX17330_NIPRTTH1_ODCP_POS)
#define MAX17330_NIPRTTH1_OCCP_POS 8
#define MAX17330_NIPRTTH1_OCCP (0xFF << MAX17330_NIPRTTH1_OCCP_POS)

/* nODSCTh register bits for MAX17330 */
#define MAX17330_NODSCTH_ODTH_POS 0
#define MAX17330_NODSCTH_ODTH (0x1F << MAX17330_NODSCTH_ODTH_POS)
#define MAX17330_NODSCTH_SCTH_POS 5
#define MAX17330_NODSCTH_SCTH (0x1F << MAX17330_NODSCTH_SCTH_POS)
#define MAX17330_NODSCTH_OCTH_POS 10
#define MAX17330_NODSCTH_OCTH (0x3F << MAX17330_NODSCTH_OCTH_POS)

/* nVPrtTh1 register bits for MAX17330 */
#define MAX17330_NVPRTTH1_UVSHDN_POS 0
#define MAX17330_NVPRTTH1_UVSHDN (0xF << MAX17330_NVPRTTH1_UVSHDN_POS)
#define MAX17330_NVPRTTH1_UOCVP_POS 4
#define MAX17330_NVPRTTH1_UOCVP (0x1F << MAX17330_NVPRTTH1_UOCVP_POS)
#define MAX17330_NVPRTTH1_UVP_POS 10
#define MAX17330_NVPRTTH1_UVP (0x3F << MAX17330_NVPRTTH1_UVP_POS)

/* nDelayCfg register bits for MAX17330 */
#define MAX17330_NDELAYCFG_UVP_POS 0
#define MAX17330_NDELAYCFG_UVP (0x3 << MAX17330_NDELAYCFG_UVP_POS)
#define MAX17330_NDELAYCFG_TEMP_POS 2
#define MAX17330_NDELAYCFG_TEMP (0x3 << MAX17330_NDELAYCFG_TEMP_POS)
#define MAX17330_NDELAYCFG_PERMFAIL_POS 4
#define MAX17330_NDELAYCFG_PERMFAIL (0x3 << MAX17330_NDELAYCFG_PERMFAIL_POS)
#define MAX17330_NDELAYCFG_OVERCURR_POS 6
#define MAX17330_NDELAYCFG_OVERCURR (0x7 << MAX17330_NDELAYCFG_OVERCURR_POS)
#define MAX17330_NDELAYCFG_OVP_POS 9
#define MAX17330_NDELAYCFG_OVP (0x3 << MAX17330_NDELAYCFG_OVP_POS)
#define MAX17330_NDELAYCFG_FULL_POS 11
#define MAX17330_NDELAYCFG_FULL (0x7 << MAX17330_NDELAYCFG_FULL_POS)
#define MAX17330_NDELAYCFG_CHGWDT_POS 14
#define MAX17330_NDELAYCFG_CHGWDT (0x3 << MAX17330_NDELAYCFG_CHGWDT_POS)

/* nPackCfg register bits for MAX17330 */
#define MAX17330_NPACKCFG_R100_POS 11
#define MAX17330_NPACKCFG_R100 (1 << MAX17330_NPACKCFG_R100_POS)
#define MAX17330_NPACKCFG_A1EN_POS 12
#define MAX17330_NPACKCFG_A1EN (1 << MAX17330_NPACKCFG_A1EN_POS)

/* nODSCCfg register bits for MAX17330 */
#define MAX17330_NODSCCFG_OCDLY_POS 0
#define MAX17330_NODSCCFG_OCDLY (0xF << MAX17330_NODSCCFG_OCDLY_POS)
#define MAX17330_NODSCCFG_SCDLY_POS 8
#define MAX17330_NODSCCFG_SCDLY (0xF << MAX17330_NODSCCFG_SCDLY_POS)

/* nTPrtTh1, nTPrtTh2, nTPrtTh3 register bits for MAX17330 */
#define MAX17330_NTPRTTH_THRESHOLD_MASK 0xFF
#define MAX17330_NTPRTTH1_T1_POS 0
#define MAX17330_NTPRTTH1_T4_POS 8
#define MAX17330_NTPRTTH2_T2_POS 0
#define MAX17330_NTPRTTH2_T3_POS 8
#define MAX17330_NTPRTTH3_TWARM_POS 0
#define MAX17330_NTPRTTH3_TPERMFAILHOT_POS 8

/*nJEITAC register bits for MAX17330 */
#define MAX17330_NVCFG1_ENJ_POS 7
#define MAX17330_NVCFG1_ENJ (1 << MAX17330_NVCFG1_ENJ_POS)

#define MAX1720X_VMAX_TOLERANCE 50 /* 50 mV */

/* nJEITAV register specific definitions */
#define MAX17330_ROOMCHARGINGV_DEFAULT 4200 /* 4200 mv */
#define MAX17330_ROOMCHARGINGV_MAX 4835		/* 4385 mv */
#define MAX17330_ROOMCHARGINGV_MIN 3560		/* 3560 mv */
#define MAX17330_ROOMCHARGINGV_STEP 5		/* 5 mV	*/

#define MAX17330_ROOMCHARGINGI_MAX 25500 /* 25500 mA with Rsense 1mohm */
#define MAX17330_ROOMCHARGINGI_STEP 100	 /* 100 uV	*/

/* nChgCfg0 register specific definitions */
#define MAX17330_NCHGCFG0_PREQUALVOLT_MAX 300  /* 300 mV */
#define MAX17330_NCHGCFG0_PREQUALVOLT_MIN -320 /* -320 mV */
#define MAX17330_NCHGCFG0_PREQUALVOLT_STEP 20  /* 20 mV */

/* nVPrtTh1 register specific definitions */
#define MAX17330_NVPRTTH1_UVP_MAX 3460 /* 3460 mV */
#define MAX17330_NVPRTTH1_UVP_MIN 2200 /* 2200 mV */
#define MAX17330_NVPRTTH1_UVP_STEP 20  /* 20 mV */

#define MAX17330_NVPRTTH1_UOCVP_MAX 1240 /* 1240 mV */
#define MAX17330_NVPRTTH1_UOCVP_STEP 40	 /* 40 mV */

#define MAX17330_NVPRTTH1_UVSHDN_MAX 280  /* 280 mV */
#define MAX17330_NVPRTTH1_UVSHDN_MIN -320 /* -320 mV */
#define MAX17330_NVPRTTH1_UVSHDN_STEP 40  /* 40 mV */

/* nODSCCfg register specific definitions */
#define MAX17330_NODSCCFG_OCDLY_MAX 14725 /* 14.725 ms */
#define MAX17330_NODSCCFG_OCDLY_MIN 70	  /* 70 us */
#define MAX17330_NODSCCFG_OCDLY_STEP 977  /* 977 us */

#define MAX17330_NODSCCFG_SCDLY_MAX 985 /* 985 ms */
#define MAX17330_NODSCCFG_SCDLY_MIN 70	/* 70 us */
#define MAX17330_NODSCCFG_SCDLY_STEP 61 /* 61 us */

/* nChgCfg1 register specific definitions */
#define MAX17330_NCHGCFG1_HEATLIM_MAX 15810 /* 25500 mW with Rsense 1mohm */
#define MAX17330_NCHGCFG1_HEATLIM_MIN 4000	/* 4000 mW with Rsense 1mohm */
#define MAX17330_NCHGCFG1_HEATLIM_STEP 510	/* 100 uV	*/

#define MAX17330_NCHGCFG1_FETLIM_MAX 131 /* 131 Degree */
#define MAX17330_NCHGCFG1_FETLIM_MIN 75	 /* 75 Degree */
#define MAX17330_NCHGCFG1_FETLIM_STEP 4	 /* 4 Degree	*/

#define MAX17330_NCHGCFG1_FETTHETA_MAX 3875 /* 3.875 */
#define MAX17330_NCHGCFG1_FETTHETA_MIN 0 /* 0.0 */
#define MAX17330_NCHGCFG1_FETTHETA_STEP 125 /* 0.125	*/

/* nTPrtTh1, nTPrtTh2, nTPrtTh3 register specific definitions */
#define MAX17330_NTPRTTH_MAX 15 /* 15 */

/* Slave Addresses */
#define MODELGAUGE_DATA_I2C_ADDR 0x36
#define NONVOLATILE_DATA_I2C_ADDR 0x0B

struct max1720x_platform_data {
	/*
	 * rsense in miliOhms.
	 * default 10 (if rsense = 0) as it is the recommended value by
	 * the datasheet although it can be changed by board designers.
	 */
	unsigned int rsense;
	int volt_min; /* in mV */
	int volt_max; /* in mV */
	int temp_min; /* in DegreC */
	int temp_max; /* in DegreeC */
	int soc_max;  /* in percent */
	int soc_min;  /* in percent */
	int curr_max; /* in mA */
	int curr_min; /* in mA */
};

struct max1720x_priv {
	struct i2c_client *client;
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *battery;
	struct max1720x_platform_data *pdata;
	struct work_struct init_worker;
	struct attribute_group *attr_grp;
	struct mutex lock;
	const u16 *regs;
	u8 nvmem_high_addr;
	int cycles_reg_lsb_percent;
	kernel_ulong_t driver_data;
	int (*get_charging_status)(void);
	int (*get_battery_health)(struct max1720x_priv *priv, int *health);
};

enum chip_id {
	ID_MAX1720X,
	ID_MAX1730X,
	ID_MAX17320,
	ID_MAX17330,
};

enum register_ids {
	STATUS_REG = 0,
	VALRTTH_REG,
	TALRTTH_REG,
	SALRTTH_REG,
	ATRATE_REG,
	REPCAP_REG,
	REPSOC_REG,
	COMMSTTAT_REG,
	TEMP_REG,
	VCELL_REG,
	CURRENT_REG,
	AVGCURRENT_REG,
	TTE_REG,
	CYCLES_REG,
	DESIGNCAP_REG,
	AVGVCELL_REG,
	MAXMINVOLT_REG,
	CONFIG_REG,
	CONFIG2_REG,
	TTF_REG,
	VERSION_REG,
	FULLCAPREP_REG,
	VEMPTY_REG,
	QH_REG,
	IALRTTH_REG,
	PROTSTATUS_REG,
	PROTALRTS_REG,
	ATTTE_REG,
	VFOCV_REG,
	CELL1_REG,
	CELL2_REG,
	CELL3_REG,
	CELL4_REG,
	BATT_REG,
	PCKP_REG,
	DIETEMP_REG,
	TEMP1_REG,
	TEMP2_REG,
	TEMP3_REG,
	TEMP4_REG,
	CHGSTAT_REG,
	LEAKCURRREP_REG,
	NVCFG1_REG,
	NJEITAC_REG,
	NJEITAV_REG,
	NDELAYCFG_REG,
	NRSENSE_REG,
	NCHGCFG0_REG,
	NCHGCFG1_REG,
	NVEMPTY_REG,
	NOVPRTTH_REG,
	NIPRTTH1_REG,
	NODSCTH_REG,
	NPACKCFG_REG,
	NVPRTTH1_REG,
	NODSCCFG_REG,
	NTPRTTH1_REG,
	NTPRTTH2_REG,
	NTPRTTH3_REG,
	NCONFIG_REG,
	NICHGTERM_REG,
	NRCOMP0_REG,
	NROMID0_REG,
	NROMID3_REG,
	NFULLSOCTHR_REG,
	NPROTMISCTH_REG,
	NPROTCFG_REG,
	NSTEPCHG_REG,
	NCHECKSUM_REG,
};

static int max1720x_get_battery_health(struct max1720x_priv *priv, int *health);
static int max1730x_get_battery_health(struct max1720x_priv *priv, int *health);
static int max17320_get_battery_health(struct max1720x_priv *priv, int *health);
static int max17330_get_battery_health(struct max1720x_priv *priv, int *health);

static int (*get_battery_health_handlers[])(struct max1720x_priv *priv, int *health) = {
	[ID_MAX1720X] = max1720x_get_battery_health,
	[ID_MAX1730X] = max1730x_get_battery_health,
	[ID_MAX17320] = max17320_get_battery_health,
	[ID_MAX17330] = max17330_get_battery_health,
};

static u32 register_to_read;

static irqreturn_t max1720x_irq_handler(int id, void *dev);
static irqreturn_t max17320_irq_handler(int id, void *dev);
static irqreturn_t max17330_irq_handler(int id, void *dev);

static int max1720x_regmap_write(struct max1720x_priv *priv, unsigned int reg, unsigned int val);
static int max1720x_regmap_read(struct max1720x_priv *priv, unsigned int reg, unsigned int *val);

static irqreturn_t (*irq_handlers[])(int id, void *dev) = {
	[ID_MAX1720X] = max1720x_irq_handler,
	[ID_MAX1730X] = max1720x_irq_handler,
	[ID_MAX17320] = max17320_irq_handler,
	[ID_MAX17330] = max17330_irq_handler,
};

/* Register addresses  */
static const u16 max1720x_regs[] = {
	[STATUS_REG] = 0x00,
	[VALRTTH_REG] = 0x01,
	[TALRTTH_REG] = 0x02,
	[SALRTTH_REG] = 0x03,
	[ATRATE_REG] = 0x04,
	[REPCAP_REG] = 0x05,
	[REPSOC_REG] = 0x06,
	[TEMP_REG] = 0x08,
	[VCELL_REG] = 0x09,
	[CURRENT_REG] = 0x0A,
	[AVGCURRENT_REG] = 0x0B,
	[TTE_REG] = 0x11,
	[CYCLES_REG] = 0x17,
	[DESIGNCAP_REG] = 0x18,
	[AVGVCELL_REG] = 0x19,
	[MAXMINVOLT_REG] = 0x1B,
	[CONFIG_REG] = 0x1D,
	[TTF_REG] = 0x20,
	[VERSION_REG] = 0x21,
	[FULLCAPREP_REG] = 0x35,
	[VEMPTY_REG] = 0x3A,
	[QH_REG] = 0x4D,
	[COMMSTTAT_REG] = 0x61,
	[IALRTTH_REG] = 0xB4,
	[ATTTE_REG] = 0xDD,
	[BATT_REG] = 0xDA,
	[VFOCV_REG] = 0xFB,
	[CELL1_REG] = 0xD8,
	[CELL2_REG] = 0xD7,
	[CELL3_REG] = 0xD6,
	[CELL4_REG] = 0xD5,
	[TEMP1_REG] = 0x134,
};

static const u16 max1730x_regs[] = {
	[STATUS_REG] = 0x00,
	[VALRTTH_REG] = 0x01,
	[TALRTTH_REG] = 0x02,
	[SALRTTH_REG] = 0x03,
	[ATRATE_REG] = 0x04,
	[REPCAP_REG] = 0x05,
	[REPSOC_REG] = 0x06,
	[TEMP_REG] = 0x1B,
	[VCELL_REG] = 0x1A,
	[CURRENT_REG] = 0x1C,
	[AVGCURRENT_REG] = 0x1D,
	[TTE_REG] = 0x11,
	[CYCLES_REG] = 0x17,
	[DESIGNCAP_REG] = 0x18,
	[AVGVCELL_REG] = 0x19,
	[MAXMINVOLT_REG] = 0x08,
	[CONFIG_REG] = 0x0B,
	[TTF_REG] = 0x20,
	[VERSION_REG] = 0x21,
	[FULLCAPREP_REG] = 0x10,
	[VEMPTY_REG] = 0x3A,
	[QH_REG] = 0x4D,
	[COMMSTTAT_REG] = 0x61,
	[IALRTTH_REG] = 0xAC,
	[PROTSTATUS_REG] = 0xD9,
	[ATTTE_REG] = 0xDD,
	[BATT_REG] = 0xDA,
	[VFOCV_REG] = 0xFB,
	[CELL1_REG] = 0xD8,
	[DIETEMP_REG] = 0x34,
	[TEMP1_REG] = 0x134,
};

static const u16 max17320_regs[] = {
	[STATUS_REG] = 0x00,
	[VALRTTH_REG] = 0x01,
	[TALRTTH_REG] = 0x02,
	[SALRTTH_REG] = 0x03,
	[ATRATE_REG] = 0x04,
	[REPCAP_REG] = 0x05,
	[REPSOC_REG] = 0x06,
	[MAXMINVOLT_REG] = 0x08,
	[CONFIG_REG] = 0x0B,
	[CONFIG2_REG] = 0xAB,
	[FULLCAPREP_REG] = 0x10,
	[TTE_REG] = 0x11,
	[CYCLES_REG] = 0x17,
	[DESIGNCAP_REG] = 0x18,
	[AVGVCELL_REG] = 0x19,
	[VCELL_REG] = 0x1A,
	[TEMP_REG] = 0x1B,
	[CURRENT_REG] = 0x1C,
	[AVGCURRENT_REG] = 0x1D,
	[TTF_REG] = 0x20,
	[VERSION_REG] = 0x21,
	[VEMPTY_REG] = 0x3A,
	[QH_REG] = 0x4D,
	[COMMSTTAT_REG] = 0x61,
	[IALRTTH_REG] = 0xAC,
	[PROTSTATUS_REG] = 0xD9,
	[PROTALRTS_REG] = 0xAF,
	[ATTTE_REG] = 0xDD,
	[BATT_REG] = 0xDA,
	[VFOCV_REG] = 0xFB,
	[CELL1_REG] = 0xD8,
	[CELL2_REG] = 0xD7,
	[CELL3_REG] = 0xD6,
	[CELL4_REG] = 0xD5,
	[PCKP_REG] = 0xDB,
	[DIETEMP_REG] = 0x34,
	[TEMP1_REG] = 0x13A,
	[TEMP2_REG] = 0x139,
	[TEMP3_REG] = 0x138,
	[TEMP4_REG] = 0x137,
};

static const u16 max17330_regs[] = {
	[STATUS_REG] = 0x00,
	[VALRTTH_REG] = 0x01,
	[TALRTTH_REG] = 0x02,
	[SALRTTH_REG] = 0x03,
	[ATRATE_REG] = 0x04,
	[REPCAP_REG] = 0x05,
	[REPSOC_REG] = 0x06,
	[MAXMINVOLT_REG] = 0x08,
	[CONFIG_REG] = 0x0B,
	[CONFIG2_REG] = 0xAB,
	[FULLCAPREP_REG] = 0x10,
	[TTE_REG] = 0x11,
	[CYCLES_REG] = 0x17,
	[DESIGNCAP_REG] = 0x18,
	[AVGVCELL_REG] = 0x19,
	[VCELL_REG] = 0x1A,
	[TEMP_REG] = 0x1B,
	[CURRENT_REG] = 0x1C,
	[AVGCURRENT_REG] = 0x1D,
	[TTF_REG] = 0x20,
	[VERSION_REG] = 0x21,
	[VEMPTY_REG] = 0x3A,
	[QH_REG] = 0x4D,
	[COMMSTTAT_REG] = 0x61,
	[IALRTTH_REG] = 0xAC,
	[PROTSTATUS_REG] = 0xD9,
	[PROTALRTS_REG] = 0xAF,
	[ATTTE_REG] = 0xDD,
	[BATT_REG] = 0xD7,
	[VFOCV_REG] = 0xFB,
	[CELL1_REG] = 0xD8,
	[PCKP_REG] = 0xDB,
	[DIETEMP_REG] = 0x34,
	[CHGSTAT_REG] = 0xA3,
	[TEMP1_REG] = 0x134,
	[LEAKCURRREP_REG] = 0x16F,
	[NICHGTERM_REG] = 0x19C,
	[NVEMPTY_REG] = 0x19E,
	[NRCOMP0_REG] = 0x1A6,
	[NCONFIG_REG] = 0x1B0,
	[NPACKCFG_REG] = 0x1B5,
	[NVCFG1_REG] = 0x1B9,
	[NROMID0_REG] = 0x1BC,
	[NROMID3_REG] = 0x1BF,
	[NCHGCFG0_REG] = 0x1C2,
	[NFULLSOCTHR_REG] = 0x1C6,
	[NCHGCFG1_REG] = 0x1CB,
	[NRSENSE_REG] = 0x1CF,
	[NVPRTTH1_REG] = 0x1D0,
	[NTPRTTH1_REG] = 0x1D1,
	[NTPRTTH3_REG] = 0x1D2,
	[NIPRTTH1_REG] = 0x1D3,
	[NTPRTTH2_REG] = 0x1D5,
	[NPROTMISCTH_REG] = 0x1D6,
	[NPROTCFG_REG] = 0x1D7,
	[NJEITAC_REG] = 0x1D8,
	[NJEITAV_REG] = 0x1D9,
	[NOVPRTTH_REG] = 0x1DA,
	[NSTEPCHG_REG] = 0x1DB,
	[NDELAYCFG_REG] = 0x1DC,
	[NODSCTH_REG] = 0x1DD,
	[NODSCCFG_REG] = 0x1DE,
	[NCHECKSUM_REG] = 0x1DF,
};

static const u16 *chip_regs[] = {
	[ID_MAX1720X] = max1720x_regs,
	[ID_MAX1730X] = max1730x_regs,
	[ID_MAX17320] = max17320_regs,
	[ID_MAX17330] = max17330_regs,
};

static const u8 nvmem_high_addrs[] = {
	[ID_MAX1720X] = 0xDF,
	[ID_MAX1730X] = 0xEF,
	[ID_MAX17320] = 0xEF,
	[ID_MAX17330] = 0xEF,
};

static const int cycles_reg_lsb_percents[] = {
	[ID_MAX1720X] = 16,
	[ID_MAX1730X] = 25,
	[ID_MAX17320] = 25,
	[ID_MAX17330] = 25,
};

static enum power_supply_property max1720x_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
};

static inline int max1720x_raw_voltage_to_uvolts(struct max1720x_priv *priv,
												 int lsb)
{
	return lsb * 625 / 8; /* 78.125uV per bit */
}

static inline int max1720x_raw_current_to_uamps(struct max1720x_priv *priv,
												int curr)
{
	return curr * 15625 / ((int)priv->pdata->rsense * 10);
}

static inline int max1720x_raw_capacity_to_uamph(struct max1720x_priv *priv,
												 int cap)
{
	return cap * 5000 / (int)priv->pdata->rsense;
}

static ssize_t log_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0, reg = 0;
	int ret = 0;
	u32 val = 0;

	for (reg = 0; reg < 0xE0; reg++) {
		ret = max1720x_regmap_read(priv, reg, &val);
		if (ret < 0)
			return ret;
		rc += (int)snprintf(buf + rc, PAGE_SIZE - rc, "0x%04X,", val);

		if (reg == 0x4F)
			reg += 0x60;

		if (reg == 0xBF)
			reg += 0x10;
	}

	rc += (int)snprintf(buf + rc, PAGE_SIZE - rc, "\n");

	return rc;
}

static ssize_t nvmem_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0, reg = 0;
	u32 val = 0;
	int ret;
	int i;


	for (reg = 0x80; reg < priv->nvmem_high_addr; reg += 16) {
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "Page %02Xh: ",
					   (reg + 0x100) >> 4);
		for (i = 0; i < 16; i++) {
			ret = max1720x_regmap_read(priv, (0x100 | (reg + i)), &val);
			if (ret < 0) {
				dev_err(dev, "NV memory reading failed (%d)\n",
						ret);
				return ret;
			}
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "0x%04X ", val);
		}
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "\n");
	}

	return rc;
}

static ssize_t atrate_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[ATRATE_REG], &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d", (short)val);
}

static ssize_t atrate_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	s32 val = 0;
	int ret;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	ret = max1720x_regmap_write(priv, priv->regs[ATRATE_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t attte_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[ATTTE_REG], &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d", (short)val);
}

static ssize_t cell_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0, reg = 0, voltage_val;
	u32 val = 0;
	int ret;
	int i;

	reg = priv->regs[CELL1_REG];
	ret = max1720x_regmap_read(priv, reg, &val);
	if (ret < 0) {
		dev_err(dev, "CELL 1 reading failed (%d)\n",
				ret);
		return ret;
	}
	voltage_val = (val * 625) / 8; /* CELL LSB: 78.125 uV */
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Cell_1 : %d\n", voltage_val);

	if (priv->driver_data == ID_MAX1720X ||
		priv->driver_data == ID_MAX17320) {
		for (i = 1; i < 4; i++) {
			ret = max1720x_regmap_read(priv, (reg - i), &val);
			if (ret < 0) {
				dev_err(dev, "CELL %d reading failed (%d)\n",
						i + 1, ret);
				return ret;
			}
			voltage_val = (val * 625) / 8; /* CELL LSB: 78.125 uV */
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Cell_%d : %d\n", (i + 1), voltage_val);
		}
	}
	return rc;
}

static ssize_t temp_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0, reg = 0, temp_val;
	u32 val = 0;
	int ret;
	int i;

	if (priv->driver_data != ID_MAX1720X) {
		reg = priv->regs[DIETEMP_REG];
		ret = max1720x_regmap_read(priv, reg, &val);
		if (ret < 0) {
			dev_err(dev, "Die Temp reading failed (%d)\n",
					ret);
			return ret;
		}
		temp_val = val / 256; /* TEMP LSB: 1/256 Celsius */
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "DieTemp : %d\n", temp_val);
	}

	reg = priv->regs[TEMP1_REG];
	ret = max1720x_regmap_read(priv, reg, &val);
	if (ret < 0) {
		dev_err(dev, "Temp 1 reading failed (%d)\n",
				ret);
		return ret;
	}
	temp_val = val / 256; /* TEMP LSB: 1/256 Celsius */
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Temp_1 : %d\n", temp_val);

	if (priv->driver_data == ID_MAX17320) {
		for (i = 1; i < 4; i++) {
			ret = max1720x_regmap_read(priv, reg - i, &val);
			if (ret < 0) {
				dev_err(dev, "Temp %d reading failed (%d)\n",
						i + 1, ret);
				return ret;
			}
			temp_val = val / 256; /* TEMP LSB: 1/256 Celsius */
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Temp_%d : %d\n", i + 1, temp_val);
		}
	}

	return rc;
}

static ssize_t batt_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;
	int batt_val;

	ret = max1720x_regmap_read(priv, priv->regs[BATT_REG], &val);
	if (ret < 0)
		return ret;

	if (priv->driver_data == ID_MAX1730X ||
		priv->driver_data == ID_MAX17330)
		batt_val = val * 1250; /* BATT LSB: 1250 uV */
	else
		batt_val = (val * 625) / 2; /* BATT LSB: 312.5 uV */
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "BATT : %d\n", batt_val);

	if (priv->driver_data == ID_MAX17320 ||
		priv->driver_data == ID_MAX17330) {
		ret = max1720x_regmap_read(priv, priv->regs[PCKP_REG], &val);
		if (ret < 0)
			return ret;

		if (priv->driver_data == ID_MAX17330)
			batt_val = val * 1250; /* BATT LSB: 1250 uV */
		else
			batt_val = (val * 625) / 2; /* BATT LSB: 312.5 uV */
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "PCKP : %d\n", batt_val);
	}

	return rc;
}

static ssize_t max17320_protstat_show(struct device *dev,
									  struct device_attribute *attr, char *buf)
{
	int rc = 0;
	u32 val = 0;
	int ret;
	struct max1720x_priv *priv = dev_get_drvdata(dev);

	ret = max1720x_regmap_read(priv, priv->regs[PROTSTATUS_REG], &val);
	if (ret < 0) {
		dev_err(dev, "Protection Status Register reading failed (%d)\n", ret);
		return ret;
	}

	if (val) {
		if (val & MAX17320_PROTSTATUS_CHGWDT)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Charge Watch Dog Timer!\n");
		if (val & MAX17320_PROTSTATUS_TOOHOTC)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for Charging!\n");
		if (val & MAX17320_PROTSTATUS_FULL)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Full Detection!\n");
		if (val & MAX17320_PROTSTATUS_TOOCOLDC)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Undertemperature!\n");
		if (val & MAX17320_PROTSTATUS_OVP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overvoltage!\n");
		if (val & MAX17320_PROTSTATUS_OCCP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overcharge Current!\n");
		if (val & MAX17320_PROTSTATUS_QOVFLW)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Q Overflow!\n");
		if (val & MAX17320_PROTSTATUS_PREQF)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Prequal timeout!\n");
		if (val & MAX17320_PROTSTATUS_IMBALANCE)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Multi-cell imbalance!\n");
		if (val & MAX17320_PROTSTATUS_PERMFAIL)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Permanent Failure!\n");
		if (val & MAX17320_PROTSTATUS_DIEHOT)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for die temperature!\n");
		if (val & MAX17320_PROTSTATUS_TOOHOTD)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for Discharging!\n");
		if (val & MAX17320_PROTSTATUS_UVP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Undervoltage Protection!\n");
		if (val & MAX17320_PROTSTATUS_ODCP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overdischarge current!\n");
		if (val & MAX17320_PROTSTATUS_RESDFAULT)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Discharging Fault!\n");
		if (val & MAX17320_PROTSTATUS_SHDN)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Shutdown Event !\n");
	} else {
		ret = max1720x_regmap_write(priv, priv->regs[PROTALRTS_REG], 0x0000);
		if (ret < 0) {
			dev_err(dev, "Protection Status Register reading failed (%d)\n", ret);
			return ret;
		}

		rc += snprintf(buf + rc, PAGE_SIZE - rc, "No Fault!\n");
	}

	return rc;
}

static ssize_t leakcurr_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;
	int leakcurr_val;

	ret = max1720x_regmap_read(priv, priv->regs[LEAKCURRREP_REG], &val);
	if (ret < 0)
		return ret;


	leakcurr_val = val * 15625 / ((int)priv->pdata->rsense * 160); /* LEAKCURR LSB: 1.5625/16 uV */
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "LEAKCURRENT : %d\n", leakcurr_val);

	return rc;
}

static ssize_t max17330_protstat_show(struct device *dev,
									  struct device_attribute *attr, char *buf)
{
	int rc = 0;
	u32 val = 0;
	int ret;
	struct max1720x_priv *priv = dev_get_drvdata(dev);

	ret = max1720x_regmap_read(priv, priv->regs[PROTSTATUS_REG], &val);
	if (ret < 0) {
		dev_err(dev, "Protection Status Register reading failed (%d)\n", ret);
		return ret;
	}

	if (val) {
		if (val & MAX17330_PROTSTATUS_CHGWDT)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Charge Watch Dog Timer!\n");
		if (val & MAX17330_PROTSTATUS_TOOHOTC)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for Charging!\n");
		if (val & MAX17330_PROTSTATUS_FULL)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Full Detection!\n");
		if (val & MAX17330_PROTSTATUS_TOOCOLDC)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Undertemperature!\n");
		if (val & MAX17330_PROTSTATUS_OVP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overvoltage!\n");
		if (val & MAX17330_PROTSTATUS_OCCP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overcharge Current!\n");
		if (val & MAX17330_PROTSTATUS_QOVFLW)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Q Overflow!\n");
		if (val & MAX17330_PROTSTATUS_PERMFAIL)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Permanent Failure!\n");
		if (val & MAX17330_PROTSTATUS_DIEHOT)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for die temperature!\n");
		if (val & MAX17330_PROTSTATUS_TOOHOTD)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overtemperature for Discharging!\n");
		if (val & MAX17330_PROTSTATUS_UVP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Undervoltage Protection!\n");
		if (val & MAX17330_PROTSTATUS_ODCP)
			rc += snprintf(buf + rc, PAGE_SIZE - rc, "Protection Alert: Overdischarge current!\n");
	} else {
		max1720x_regmap_write(priv, priv->regs[PROTALRTS_REG], 0x0000);

		max1720x_regmap_read(priv, priv->regs[STATUS_REG], &val);
		// Clear Protection alert bit of status register
		ret = max1720x_regmap_write(priv, priv->regs[STATUS_REG], val & ~MAX1720X_STATUS_PROTECTION_ALRT);
		if (ret < 0) {
			dev_err(dev, "Protection Status Register clearing failed (%d)\n", ret);
			return ret;
		}

		rc += snprintf(buf + rc, PAGE_SIZE - rc, "No Fault!\n");
	}

	return rc;
}

static ssize_t voltageVal_store(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_ROOMCHARGINGV_MAX || in_val < MAX17330_ROOMCHARGINGV_MIN)
		return -EINVAL;

	// Update Voltage
	voltage_val = in_val - MAX17330_ROOMCHARGINGV_DEFAULT;
	voltage_val /= MAX17330_ROOMCHARGINGV_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NJEITAV_REG], &val);
	if (ret < 0)
		return ret;
	val &= ~MAX17330_NJEITAV_ROOMCHARGINGV;
	val |= voltage_val << MAX17330_NJEITAV_ROOMCHARGINGV_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NJEITAV_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t voltageVal_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 voltage_val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NJEITAV_REG], &val);
	if (ret < 0)
		return ret;

	val = (val & MAX17330_NJEITAV_ROOMCHARGINGV) >> MAX17330_NJEITAV_ROOMCHARGINGV_POS;
	if ((val & 0x80) == 0x80) {
		val = (~val + 1) & 0xFF;
		val *= MAX17330_ROOMCHARGINGV_STEP;
		voltage_val = MAX17330_ROOMCHARGINGV_DEFAULT - val;
	} else {
		val *= MAX17330_ROOMCHARGINGV_STEP;
		voltage_val = MAX17330_ROOMCHARGINGV_DEFAULT + val;
	}

	rc += snprintf(buf + rc, PAGE_SIZE - rc, "VOLTAGE : %d mV\n", voltage_val);

	return rc;
}

static ssize_t currentVal_store(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int current_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	in_val = in_val * priv->pdata->rsense;

	if (in_val > MAX17330_ROOMCHARGINGI_MAX || in_val < 0)
		return -EINVAL;

	// Update Current
	current_val = in_val / MAX17330_ROOMCHARGINGI_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NJEITAC_REG], &val);
	if (ret < 0)
		return ret;
	val &= ~MAX17330_NJEITAC_ROOMCHARGINGI;
	val |= current_val << MAX17330_NJEITAC_ROOMCHARGINGI_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NJEITAC_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t currentVal_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NJEITAC_REG], &val);
	if (ret < 0)
		return ret;

	val = (val & MAX17330_NJEITAC_ROOMCHARGINGI) >> MAX17330_NJEITAC_ROOMCHARGINGI_POS;
	val = val * (MAX17330_ROOMCHARGINGI_STEP / priv->pdata->rsense);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "CURRENT : %d mA\n", val);

	return rc;
}

static ssize_t fet_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&priv->lock);
	// Update DCHG FET
	ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &read_val);
	if (ret < 0)
		goto error;

	read_val &= 0xFF06;
	// Unlock Write Protection
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);

	read_val &= ~MAX17330_COMMSTAT_CHGOFF;
	read_val |= (val << 8);
	ret = regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	if (ret < 0)
		goto error;

	read_val |= 0x00F9;
	// lock Write Protection
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);

	mutex_unlock(&priv->lock);
	return count;
error:
	mutex_unlock(&priv->lock);
	return ret;
}

static ssize_t fet_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read CHG FET
	ret = max1720x_regmap_read(priv, priv->regs[COMMSTTAT_REG], &val);
	if (ret < 0) {
		dev_err(dev, "CommStat Register reading failed (%d)\n", ret);
		return ret;
	}

	if (val & MAX17330_COMMSTAT_CHGOFF)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "COMM STATUS : CHGOff 1!\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "COMM STATUS : CHGOff 0!\n");

	return rc;
}

static ssize_t dfet_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&priv->lock);
	// Update DCHG FET
	ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &read_val);
	if (ret < 0)
		goto error;

	read_val &= 0xFF06;
	// Unlock Write Protection
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);

	read_val &= ~MAX17330_COMMSTAT_DISOFF;
	read_val |= (val << 9);
	ret = regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	if (ret < 0)
		goto error;

	read_val |= 0x00F9;
	// lock Write Protection
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], read_val);

	mutex_unlock(&priv->lock);
	return count;
error:
	mutex_unlock(&priv->lock);
	return ret;
}

static ssize_t dfet_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read DCHG FET

	ret = max1720x_regmap_read(priv, priv->regs[COMMSTTAT_REG], &val);
	if (ret < 0) {
		dev_err(dev, "CommStat Register reading failed (%d)\n", ret);
		return ret;
	}

	if (val & MAX17330_COMMSTAT_DISOFF)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "COMM STATUS : DISOff 1!\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "COMM STATUS : DISOff 0!\n");

	return rc;
}

static ssize_t ship_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	// Update Ship mode
	ret = max1720x_regmap_read(priv, priv->regs[CONFIG_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~MAX17330_CONFIG_SHIP;
	read_val |= (val << 7);
	ret = max1720x_regmap_write(priv, priv->regs[CONFIG_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ship_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read Ship mode
	ret = max1720x_regmap_read(priv, priv->regs[CONFIG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "CommStat Register reading failed (%d)\n", ret);
		return ret;
	}

	if (val & MAX17330_CONFIG_SHIP)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "CONFIG : SHIP 1!\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "CONFIG : SHIP 0!\n");

	return rc;
}

static ssize_t register_read_store(struct device *dev,
							  struct device_attribute *attr,
							  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 register_val = 0;

	if (kstrtouint(buf, 0, &register_val))
		return -EINVAL;

	if (register_val > 0xFF) {
		if ((register_val & 0xFF) < 0x80 || (register_val & 0xFF) > priv->nvmem_high_addr)
			return -EINVAL;
	} else {
		if (register_val > 0xD9)
			return -EINVAL;
	}
	register_to_read = register_val;

	return count;
}

static ssize_t register_read_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 register_val = 0;
	u32 data_val = 0;
	int ret;
	int rc = 0;

	register_val = register_to_read;

	// Read requested register value
	ret = max1720x_regmap_read(priv, register_val, &data_val);
	if (ret < 0) {
		dev_err(dev, "Register reading failed (%d)\n", ret);
		rc = ret;
	} else {
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "Register Value[0x%X] : 0x%X!\n", register_to_read, data_val);
	}

	return rc;
}

static ssize_t register_store_store(struct device *dev,
							  struct device_attribute *attr,
							  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	char local_buf[16], *str = local_buf;
	u32 register_val = 0;
	u32 data_val = 0;
	u32 commstat_val;
	u32 recover_val = 0;
	int ret;
	char *register_addr, *data, *recover;

	if (strnlen(buf, sizeof(local_buf)) >= sizeof(local_buf))
		return -EINVAL;

	strcpy(str, buf);

	register_addr = strsep(&str, " ");
	data = strsep(&str, " ");
	recover = strsep(&str, " ");

	if (register_addr == NULL || data == NULL || recover == NULL)
		return -EINVAL;

	// Too many arguements
	if (str)
		return -EINVAL;

	if (kstrtouint(register_addr, 16, &register_val))
		return -EINVAL;
	if (kstrtouint(data, 16, &data_val))
		return -EINVAL;
	if (kstrtouint(recover, 0, &recover_val))
		return -EINVAL;

	if ((data_val > 0xFFFF) || (recover_val != 0 && recover_val != 1))
		return -EINVAL;

	// NonVolatile Register
	if (register_val > 0xFFFF) {
		return -EINVAL;
	} else if (register_val > 0xFF) {
		if ((register_val & 0xFF) < 0x80 || (register_val & 0xFF) > priv->nvmem_high_addr)
			return -EINVAL;
	} else {
		if (register_val > 0xD9)
			return -EINVAL;
	}

	mutex_lock(&priv->lock);
	ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &commstat_val);
	if (ret < 0)
		goto error;

	// Unlock Write Protection
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val & 0xFF06);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val & 0xFF06);

	if ((register_val & 0x100) == 0x100) {
		priv->client->addr = NONVOLATILE_DATA_I2C_ADDR;
		ret = regmap_write(priv->regmap, (register_val & 0xFF), data_val);
		priv->client->addr = MODELGAUGE_DATA_I2C_ADDR;
	} else {
		ret = regmap_write(priv->regmap, register_val, data_val);
	}
	if (ret < 0)
		goto error;

	if (recover_val == 1) {
		// Firmware Restart
		regmap_update_bits(priv->regmap, priv->regs[CONFIG2_REG],
						   MAX1720X_CONFIG2_POR_CMD,
						   MAX1720X_CONFIG2_POR_CMD);

		// Wait 500 ms for POR_CMD to clear;
		mdelay(500);
	}

	// Lock Write Protection
	if (register_val == priv->regs[COMMSTTAT_REG])
		commstat_val = data_val;
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val | 0x00F9);
	regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val | 0x00F9);

	mutex_unlock(&priv->lock);
	return count;
error:
	mutex_unlock(&priv->lock);
	return ret;
}

static ssize_t delay_config_store(struct device *dev,
								  struct device_attribute *attr,
								  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	ret = max1720x_regmap_write(priv, priv->regs[NDELAYCFG_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t delay_config_show(struct device *dev,
								 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	u8 delay_val = 0;
	u32 low_val = 0;
	u32 high_val = 0;
	int ret;

	// Read Delay Config register
	ret = max1720x_regmap_read(priv, priv->regs[NDELAYCFG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "Delay Config Register reading failed (%d)\n", ret);
		return ret;
	}
	delay_val = (val & MAX17330_NDELAYCFG_UVP) >> MAX17330_NDELAYCFG_UVP_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 351 : 0;
	high_val = (1 << delay_val) * 351;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "UVP Timer : %dms to %dms!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_TEMP) >> MAX17330_NDELAYCFG_TEMP_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 351 : 0;
	high_val = (1 << delay_val) * 351;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Temp Timer : %dmsto %dms!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_PERMFAIL) >> MAX17330_NDELAYCFG_PERMFAIL_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 351 : 0;
	high_val = (1 << delay_val) * 351;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Permanent failure Timer : %dms to %dms!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_OVERCURR) >> MAX17330_NDELAYCFG_OVERCURR_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 351 : 0;
	high_val = (1 << delay_val) * 351;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Overcurrent Timer : %dms to %dms!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_OVP) >> MAX17330_NDELAYCFG_OVP_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 351 : 0;
	high_val = (1 << delay_val) * 351;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Overvoltage protection Timer : %dms to %dms!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_FULL) >> MAX17330_NDELAYCFG_FULL_POS;
	low_val = delay_val ? (1 << (delay_val - 1)) * 67 : 33;
	high_val = delay_val ? (1 << (delay_val - 1)) * 90 : 44;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Full protection Timer : %ds to %ds!\n", low_val, high_val);

	delay_val = (val & MAX17330_NDELAYCFG_CHGWDT) >> MAX17330_NDELAYCFG_CHGWDT_POS;
	low_val = (1 << delay_val) * 11200;
	high_val = low_val * 2;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Charger Comm Watchdog protection Timer : %dms to %dms!\n", low_val, high_val);

	low_val = (1 << delay_val) * 2800;
	high_val = low_val * 2;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Charger removal Timer : %dms to %dms!\n", low_val, high_val);

	return rc;
}

static ssize_t rsense_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 0xFFFF)
		return -EINVAL;

	val = val / 10;
	ret = max1720x_regmap_write(priv, priv->regs[NRSENSE_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t rsense_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read sense resistor value
	ret = max1720x_regmap_read(priv, priv->regs[NRSENSE_REG], &val);
	if (ret < 0) {
		dev_err(dev, "RSense Register reading failed (%d)\n", ret);
		return ret;
	}

	val = val * 10;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Charger removal Timer : %d uohm!\n", val);

	return rc;
}

static ssize_t recovery_voltage_store(struct device *dev,
									  struct device_attribute *attr,
									  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 5080)
		return -EINVAL;

	// Read Empty Voltage registor value
	ret = max1720x_regmap_read(priv, priv->regs[NVEMPTY_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NVEMPTY_VR);
	read_val |= (val / 40) << MAX17330_NVEMPTY_VR_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NVEMPTY_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t recovery_voltage_show(struct device *dev,
									 struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read Empty Voltage registor value
	ret = max1720x_regmap_read(priv, priv->regs[NVEMPTY_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVEmpty Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NVEMPTY_VR) >> MAX17330_NVEMPTY_VR_POS;
	val = val * 40;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Recovery Voltage : %dmV!\n", val);

	return rc;
}

static ssize_t empty_voltage_store(struct device *dev,
								   struct device_attribute *attr,
								   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 5110)
		return -EINVAL;

	// Read Empty Voltage registor value
	ret = max1720x_regmap_read(priv, priv->regs[NVEMPTY_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NVEMPTY_VE);
	read_val |= (val / 10) << MAX17330_NVEMPTY_VE_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NVEMPTY_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t empty_voltage_show(struct device *dev,
								  struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read Empty Voltage registor value
	ret = max1720x_regmap_read(priv, priv->regs[NVEMPTY_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVEmpty Register reading failed (%d)\n", ret);
		rc = ret;
	}
	val = (val & MAX17330_NVEMPTY_VE) >> MAX17330_NVEMPTY_VE_POS;
	val = val * 10;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Empty Voltage : %dmV!\n", val);

	return rc;
}

static ssize_t dOVP_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 150)
		return -EINVAL;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NOVPRTTH_DOVP);
	read_val |= (val / 10) << MAX17330_NOVPRTTH_DOVP_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NOVPRTTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t dOVP_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nOVPrtTh Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NOVPRTTH_DOVP) >> MAX17330_NOVPRTTH_DOVP_POS;
	val = val * 10;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Delta from ChargeVoltage to Overvoltage Protection : %d mV!\n", val);

	return rc;
}

static ssize_t dOVPR_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 150)
		return -EINVAL;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NOVPRTTH_DOVPR);
	read_val |= (val / 10) << MAX17330_NOVPRTTH_DOVPR_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NOVPRTTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t dOVPR_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nOVPrtTh Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NOVPRTTH_DOVPR) >> MAX17330_NOVPRTTH_DOVPR_POS;
	val = val * 10;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Delta from Overvoltage Protection to the Overvoltage-Release Threshold : %d mV!\n", val);

	return rc;
}

static ssize_t OVP_perm_fail_store(struct device *dev,
								   struct device_attribute *attr,
								   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 340 || val < 40)
		return -EINVAL;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NOVPRTTH_OVPPERMFAIL);
	read_val |= ((val - 40) / 20) << MAX17330_NOVPRTTH_OVPPERMFAIL_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NOVPRTTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t OVP_perm_fail_show(struct device *dev,
								  struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nOVPrtTh Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NOVPRTTH_OVPPERMFAIL) >> MAX17330_NOVPRTTH_OVPPERMFAIL_POS;
	val = (val * 20) + 40;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Permanent Failure OVP Threshold : %d mV!\n", val);

	return rc;
}

static ssize_t ChgDetTh_store(struct device *dev,
							  struct device_attribute *attr,
							  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 80 || val < 10)
		return -EINVAL;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~(MAX17330_NOVPRTTH_CHGDETTH);
	read_val |= ((val - 10) / 10) << MAX17330_NOVPRTTH_CHGDETTH_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NOVPRTTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ChgDetTh_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	// Read permanent overvoltage protection threshold register values
	ret = max1720x_regmap_read(priv, priv->regs[NOVPRTTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nOVPrtTh Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NOVPRTTH_CHGDETTH) >> MAX17330_NOVPRTTH_CHGDETTH_POS;
	val = (val * 10) + 10;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Charger Detection Threshold : %d mV!\n", val);

	return rc;
}

static ssize_t OCTH_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > 39375)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &read_val);
	if (ret < 0)
		return ret;

	val = (39375 - val) / 625;
	read_val &= ~(MAX17330_NODSCTH_OCTH);
	read_val |= val << MAX17330_NODSCTH_OCTH_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NODSCTH_REG], read_val);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t OCTH_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{

	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nODSCTh Register reading failed (%d)\n", ret);
		return ret;
	}
	val = (val & MAX17330_NODSCTH_OCTH) >> MAX17330_NOVPRTTH_CHGDETTH_POS;
	val = 39375 - (val * 625);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Overcharge Threshold : %d uV!\n", val);

	return rc;
}

static ssize_t ODTH_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val > 0 || val < -79360)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &read_val);
	if (ret < 0)
		return ret;

	val = (val + 79360) / 2560;
	read_val &= ~(MAX17330_NODSCTH_ODTH);
	read_val |= val << MAX17330_NODSCTH_ODTH_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NODSCTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ODTH_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nODSCTh Register reading failed (%d)\n", ret);
		return ret;
	}

	val = (val & MAX17330_NODSCTH_ODTH) >> MAX17330_NODSCTH_ODTH_POS;
	val = (val * 2560) - 79360;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Overdischarge Threshold : %d uV!\n", val);

	return rc;
}

static ssize_t SCTH_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val > 0 || val < -158720)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &read_val);
	if (ret < 0)
		return ret;

	val = (val + 158720) / 5120;
	read_val &= ~(MAX17330_NODSCTH_SCTH);
	read_val |= val << MAX17330_NODSCTH_SCTH_POS;

	ret = max1720x_regmap_write(priv, priv->regs[NODSCTH_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t SCTH_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;


	ret = max1720x_regmap_read(priv, priv->regs[NODSCTH_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nODSCTh Register reading failed (%d)\n", ret);
		return ret;
	}
	val = (val & MAX17330_NODSCTH_SCTH) >> MAX17330_NODSCTH_SCTH_POS;
	val = (val * 5120) - 158720;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Short-Circuit Threshold : %d uV!\n", val);

	return rc;
}

static ssize_t A1EN_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NPACKCFG_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~MAX17330_NPACKCFG_A1EN;
	read_val |= (val << MAX17330_NPACKCFG_A1EN_POS);
	ret = max1720x_regmap_write(priv, priv->regs[NPACKCFG_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t A1EN_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NPACKCFG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nPackCfg Register reading failed (%d)\n", ret);
		return ret;
	}
	if (val & MAX17330_NPACKCFG_A1EN)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "AIN1 Channel Enable !\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "AIN1 Channel Disable !\n");

	return rc;
}

static ssize_t R100_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NPACKCFG_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~MAX17330_NPACKCFG_R100;
	read_val |= (val << MAX17330_NPACKCFG_R100_POS);
	ret = max1720x_regmap_write(priv, priv->regs[NPACKCFG_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t R100_show(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NPACKCFG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nPackCfg Register reading failed (%d)\n", ret);
		return ret;
	}
	if (val & MAX17330_NPACKCFG_R100)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "R100: Using 10 kohm !\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "R100: Negative Temperature Coefficient !\n");

	return rc;
}

static ssize_t UVP_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NVPRTTH1_UVP_MAX || in_val < MAX17330_NVPRTTH1_UVP_MIN)
		return -EINVAL;

	// calculate register value
	voltage_val = (in_val - MAX17330_NVPRTTH1_UVP_MIN) / MAX17330_NVPRTTH1_UVP_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NVPRTTH1_UVP;
	val |= voltage_val << MAX17330_NVPRTTH1_UVP_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NVPRTTH1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t UVP_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVPrtTh1 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get uvp register value
	val = (val & MAX17330_NVPRTTH1_UVP) >> MAX17330_NVPRTTH1_UVP_POS;
	// Calculate UVP Voltage
	val = (val * MAX17330_NVPRTTH1_UVP_STEP) + MAX17330_NVPRTTH1_UVP_MIN;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "UnderVoltage Protection Threshold : %d mV\n", val);

	return rc;
}

static ssize_t UOCVP_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtouint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NVPRTTH1_UOCVP_MAX)
		return -EINVAL;

	// calculate register value
	voltage_val = in_val / MAX17330_NVPRTTH1_UOCVP_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NVPRTTH1_UOCVP;
	val |= voltage_val << MAX17330_NVPRTTH1_UOCVP_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NVPRTTH1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t UOCVP_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVPrtTh1 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get uocvp register value
	val = (val & MAX17330_NVPRTTH1_UOCVP) >> MAX17330_NVPRTTH1_UOCVP_POS;
	voltage_val = (val * MAX17330_NVPRTTH1_UOCVP_STEP);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Under Open Circuit Voltage Protection Threshold : %d mV\n", voltage_val);

	return rc;
}

static ssize_t UVShdn_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NVPRTTH1_UVSHDN_MAX || in_val < MAX17330_NVPRTTH1_UVSHDN_MIN)
		return -EINVAL;

	// calculate register value
	voltage_val = in_val / MAX17330_NVPRTTH1_UVSHDN_STEP;
	if (in_val < 0)
		voltage_val = (~voltage_val) + 1;

	// Clear overflow bit if necessary
	voltage_val &= MAX17330_NVPRTTH1_UVSHDN >> MAX17330_NVPRTTH1_UVSHDN_POS;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NVPRTTH1_UVSHDN;
	val |= voltage_val << MAX17330_NVPRTTH1_UVSHDN_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NVPRTTH1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t UVShdn_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NVPRTTH1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVPrtTh1 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get uocvp register value
	val = (val & MAX17330_NVPRTTH1_UVSHDN) >> MAX17330_NVPRTTH1_UVSHDN_POS;
	// Calculate UVSHDN Voltage
	if (val & (((MAX17330_NVPRTTH1_UVSHDN >> MAX17330_NVPRTTH1_UVSHDN_POS) + 1) >> 1))
		// Negative
		voltage_val = (~val) + 1;
	else
		voltage_val = val;
	voltage_val = (voltage_val * MAX17330_NVPRTTH1_UVSHDN_STEP);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "UnderVoltage Shutdown Threshold : %d mV\n", voltage_val);

	return rc;
}

static ssize_t OCDLY_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int delay_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NODSCCFG_OCDLY_MAX || in_val < MAX17330_NODSCCFG_OCDLY_MIN)
		return -EINVAL;

	// calculate register value
	delay_val = (in_val - MAX17330_NODSCCFG_OCDLY_MIN) / MAX17330_NODSCCFG_OCDLY_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCCFG_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NODSCCFG_OCDLY;
	val |= delay_val << MAX17330_NODSCCFG_OCDLY_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NODSCCFG_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t OCDLY_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCCFG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nODSCCfg Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get OCDLY register value
	val = (val & MAX17330_NODSCCFG_OCDLY) >> MAX17330_NODSCCFG_OCDLY_POS;
	// Calculate OCDLY
	val = (val * MAX17330_NODSCCFG_OCDLY_STEP) + MAX17330_NODSCCFG_OCDLY_MIN;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Overdischarge and Overcharge Current Delay : %d us\n", val);

	return rc;
}

static ssize_t SCDLY_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int delay_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NODSCCFG_SCDLY_MAX || in_val < MAX17330_NODSCCFG_SCDLY_MIN)
		return -EINVAL;

	// calculate register value
	delay_val = (in_val - MAX17330_NODSCCFG_SCDLY_MIN) / MAX17330_NODSCCFG_SCDLY_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCCFG_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NODSCCFG_SCDLY;
	val |= delay_val << MAX17330_NODSCCFG_SCDLY_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NODSCCFG_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t SCDLY_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NODSCCFG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nODSCCfg Register reading failed (%d)\n", ret);
		return ret;
	}

	// Get SCDLY register value
	val = (val & MAX17330_NODSCCFG_SCDLY) >> MAX17330_NODSCCFG_SCDLY_POS;
	// Calculate SCDLY
	val = (val * MAX17330_NODSCCFG_SCDLY_STEP) + MAX17330_NODSCCFG_SCDLY_MIN;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Short-Circuit Delay : %d us\n", val);
	return rc;
}

static ssize_t threshold_temperature_store(struct max1720x_priv *priv,
										   unsigned int reg, unsigned int in_val, u8 bit_position)
{
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NTPRTTH_THRESHOLD_MASK;
	val |= in_val << bit_position;
	ret = max1720x_regmap_write(priv, reg, val);
	if (ret < 0)
		return ret;

	return 0;
}

static ssize_t threshold_temperature_show(char *buf, struct max1720x_priv *priv, unsigned int reg, u8 bit_position)
{
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, reg, &val);
	if (ret < 0)
		return ret;

	// Get temperature register value
	val = (val & MAX17330_NTPRTTH_MAX) >> bit_position;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Temperature : %d Degree\n", val);

	return rc;
}

static ssize_t T1_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH1_REG], in_val, MAX17330_NTPRTTH1_T1_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t T1_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH1_REG], MAX17330_NTPRTTH1_T1_POS);
	return ret;
}

static ssize_t T4_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH1_REG], in_val, MAX17330_NTPRTTH1_T4_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t T4_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH1_REG], MAX17330_NTPRTTH1_T4_POS);
	return ret;
}

static ssize_t T2_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH2_REG], in_val, MAX17330_NTPRTTH2_T2_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t T2_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH2_REG], MAX17330_NTPRTTH2_T2_POS);
	return ret;
}

static ssize_t T3_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH2_REG], in_val, MAX17330_NTPRTTH2_T3_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t T3_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH2_REG], MAX17330_NTPRTTH2_T3_POS);
	return ret;
}

static ssize_t Twarm_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH3_REG], in_val, MAX17330_NTPRTTH3_TWARM_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t Twarm_show(struct device *dev,
						  struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH3_REG], MAX17330_NTPRTTH3_TWARM_POS);
	return ret;
}

static ssize_t TpermFailHot_store(struct device *dev,
								  struct device_attribute *attr,
								  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NTPRTTH_MAX)
		return -EINVAL;

	ret = threshold_temperature_store(priv, priv->regs[NTPRTTH3_REG], in_val, MAX17330_NTPRTTH3_TPERMFAILHOT_POS);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t TpermFailHot_show(struct device *dev,
								 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret = 0;

	ret = threshold_temperature_show(buf, priv, priv->regs[NTPRTTH3_REG], MAX17330_NTPRTTH3_TPERMFAILHOT_POS);
	return ret;
}

static ssize_t PreQualVolt_store(struct device *dev,
								 struct device_attribute *attr,
								 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NCHGCFG0_PREQUALVOLT_MAX || in_val < MAX17330_NCHGCFG0_PREQUALVOLT_MIN)
		return -EINVAL;

	// calculate register value
	voltage_val = in_val / MAX17330_NCHGCFG0_PREQUALVOLT_STEP;
	if (in_val < 0)
		voltage_val = (~voltage_val) + 1;

	// Clear overflow bit if necessary
	voltage_val &= MAX17330_NCHGCFG0_PREQUALVOLT >> MAX17330_NCHGCFG0_PREQUALVOLT_POS;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NCHGCFG0_PREQUALVOLT;
	val |= voltage_val << MAX17330_NCHGCFG0_PREQUALVOLT_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG0_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t PreQualVolt_show(struct device *dev,
								struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg0 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get PreQualVolt register value
	val = (val & MAX17330_NCHGCFG0_PREQUALVOLT) >> MAX17330_NCHGCFG0_PREQUALVOLT_POS;
	// Calculate PreQualVolt Voltage
	if (val & (((MAX17330_NCHGCFG0_PREQUALVOLT >> MAX17330_NCHGCFG0_PREQUALVOLT_POS) + 1) >> 1))
		// Negative
		voltage_val = (~val) + 1;
	else
		voltage_val = val;
	voltage_val = (voltage_val * MAX17330_NCHGCFG0_PREQUALVOLT_STEP);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Prequal Voltage : %d mV\n", voltage_val);

	return rc;
}

static ssize_t VSysMin_store(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > 0 || in_val < -700)
		return -EINVAL;

	// calculate register value
	voltage_val = in_val / 100;
	voltage_val = 0 - voltage_val;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NCHGCFG0_VSYSMIN;
	val |= voltage_val << MAX17330_NCHGCFG0_VSYSMIN_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG0_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t VSysMin_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int voltage_val;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg0 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get VSysMin register value
	val = (val & MAX17330_NCHGCFG0_VSYSMIN) >> MAX17330_NCHGCFG0_VSYSMIN_POS;
	voltage_val = (val * 100);
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Minimum System Voltage relative to nVempty : -%d mV\n", voltage_val);

	return rc;
}

static ssize_t PreChgCurr_store(struct device *dev,
								struct device_attribute *attr,
								const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 in_val;
	int voltage_val = 0;
	int ret;

	if (kstrtouint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > 31)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NCHGCFG0_PRECHGCURR;
	val |= voltage_val << MAX17330_NCHGCFG0_PRECHGCURR_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG0_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t PreChgCurr_show(struct device *dev,
							   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG0_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg0 Register reading failed (%d)\n", ret);
		return ret;
	}
	// Get PreChgCurr register value
	val = (val & MAX17330_NCHGCFG0_PRECHGCURR) >> MAX17330_NCHGCFG0_PRECHGCURR_POS;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "PreChgCurr : %d\n", val);

	return rc;
}

static ssize_t HeatLim_store(struct device *dev,
							 struct device_attribute *attr,
							 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int heat_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	in_val = in_val * priv->pdata->rsense;

	if (in_val > MAX17330_NCHGCFG1_HEATLIM_MAX || in_val < MAX17330_NCHGCFG1_HEATLIM_MIN)
		return -EINVAL;

	// Update register value
	heat_val = (in_val - MAX17330_NCHGCFG1_HEATLIM_MIN) / MAX17330_NCHGCFG1_HEATLIM_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0)
		return ret;
	val &= ~MAX17330_NCHGCFG1_HEATLIM;
	val |= heat_val << MAX17330_NCHGCFG1_HEATLIM_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t HeatLim_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg1 Register reading failed (%d)\n", ret);
		return ret;
	}
	val = (val & MAX17330_NCHGCFG1_HEATLIM) >> MAX17330_NCHGCFG1_HEATLIM_POS;
	val = val * (MAX17330_NCHGCFG1_HEATLIM_STEP / priv->pdata->rsense);
	val += MAX17330_NCHGCFG1_HEATLIM_MIN / priv->pdata->rsense;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "Heat Limit : %d mW\n", val);

	return rc;
}

static ssize_t FETLim_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int heat_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NCHGCFG1_FETLIM_MAX || in_val < MAX17330_NCHGCFG1_FETLIM_MIN)
		return -EINVAL;

	// Update register value
	heat_val = (in_val - MAX17330_NCHGCFG1_FETLIM_MIN) / MAX17330_NCHGCFG1_FETLIM_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0)
		return ret;

	val &= ~MAX17330_NCHGCFG1_FETLIM;
	val |= heat_val << MAX17330_NCHGCFG1_FETLIM_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t FETLim_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg1 Register reading failed (%d)\n", ret);
		return ret;
	}
	val = (val & MAX17330_NCHGCFG1_FETLIM) >> MAX17330_NCHGCFG1_FETLIM_POS;
	val = (val * MAX17330_NCHGCFG1_FETLIM_STEP) + MAX17330_NCHGCFG1_FETLIM_MIN;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "FET temperature Limit : %d mW\n", val);

	return rc;
}

static ssize_t FetTheta_store(struct device *dev,
							  struct device_attribute *attr,
							  const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int heat_val = 0;
	int ret, in_val;

	if (kstrtoint(buf, 0, &in_val))
		return -EINVAL;

	if (in_val > MAX17330_NCHGCFG1_FETTHETA_MAX || in_val < MAX17330_NCHGCFG1_FETTHETA_MIN)
		return -EINVAL;

	// Update register value
	heat_val = (in_val - MAX17330_NCHGCFG1_FETTHETA_MIN) / MAX17330_NCHGCFG1_FETTHETA_STEP;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0)
		return ret;
	val &= ~MAX17330_NCHGCFG1_FETTHETA;
	val |= heat_val << MAX17330_NCHGCFG1_FETTHETA_POS;
	ret = max1720x_regmap_write(priv, priv->regs[NCHGCFG1_REG], val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t FetTheta_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	int ret, rc = 0;

	ret = max1720x_regmap_read(priv, priv->regs[NCHGCFG1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nChgCfg1 Register reading failed (%d)\n", ret);
		return ret;
	}
	val = (val & MAX17330_NCHGCFG1_FETTHETA) >> MAX17330_NCHGCFG1_FETTHETA_POS;
	val = (val * MAX17330_NCHGCFG1_FETTHETA_STEP) + MAX17330_NCHGCFG1_FETTHETA_MIN;
	rc += snprintf(buf + rc, PAGE_SIZE - rc, "FetTheta : %d\n", val);

	return rc;
}

static ssize_t push_button_store(struct device *dev,
								 struct device_attribute *attr,
								 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NCONFIG_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~MAX17330_NCONFIG_PBEN;
	read_val |= (val << MAX17330_NCONFIG_PBEN_POS);
	ret = max1720x_regmap_write(priv, priv->regs[NCONFIG_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t push_button_show(struct device *dev,
								struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NCONFIG_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nConfig Register reading failed (%d)\n", ret);
		return ret;
	}
	if (val & MAX17330_NCONFIG_PBEN)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "PushButton Enable !\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "PushButton Disable !\n");

	return rc;
}

static ssize_t enJ_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	u32 val = 0;
	u32 read_val = 0;
	int ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	ret = max1720x_regmap_read(priv, priv->regs[NVCFG1_REG], &read_val);
	if (ret < 0)
		return ret;

	read_val &= ~MAX17330_NVCFG1_ENJ;
	read_val |= (val << MAX17330_NVCFG1_ENJ_POS);
	ret = max1720x_regmap_write(priv, priv->regs[NVCFG1_REG], read_val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t enJ_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int rc = 0;
	u32 val = 0;
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[NVCFG1_REG], &val);
	if (ret < 0) {
		dev_err(dev, "nVCFG1 Register reading failed (%d)\n", ret);
		return ret;
	}
	if (val & MAX17330_NVCFG1_ENJ)
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "ChargingCurrent and ChargingVoltage Enabled !\n");
	else
		rc += snprintf(buf + rc, PAGE_SIZE - rc, "ChargingCurrent and ChargingVoltage Disabled !\n");

	return rc;
}

static ssize_t control_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct max1720x_priv *priv = dev_get_drvdata(dev);
	int ret;
	u32 commstat_val;

	if (sysfs_streq(buf, "restart")) {
		mutex_lock(&priv->lock);
		ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &commstat_val);
		if (ret < 0) {
			mutex_unlock(&priv->lock);
			return ret;
		}

		// Unlock Write Protection
		regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val & 0xFF06);
		regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val & 0xFF06);

		// Firmware Restart
		regmap_update_bits(priv->regmap, priv->regs[CONFIG2_REG],
							MAX1720X_CONFIG2_POR_CMD,
							MAX1720X_CONFIG2_POR_CMD);

		// Wait 500 ms for POR_CMD to clear;
		mdelay(500);

		// Lock Write Protection
		regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val | 0x00F9);
		regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], commstat_val | 0x00F9);
		mutex_unlock(&priv->lock);
	} else {
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR(log, 0444, log_show, NULL);
static DEVICE_ATTR(nvmem, 0444, nvmem_show, NULL);
static DEVICE_ATTR(atrate, 0644, atrate_show, atrate_store);
static DEVICE_ATTR(voltageVal, 0644, voltageVal_show, voltageVal_store);
static DEVICE_ATTR(currentVal, 0644, currentVal_show, currentVal_store);
static DEVICE_ATTR(ship, 0644, ship_show, ship_store);
static DEVICE_ATTR(fet, 0644, fet_show, fet_store);
static DEVICE_ATTR(dfet, 0644, dfet_show, dfet_store);
static DEVICE_ATTR(attte, 0444, attte_show, NULL);
static DEVICE_ATTR(cell, 0444, cell_show, NULL);
static DEVICE_ATTR(temp, 0444, temp_show, NULL);
static DEVICE_ATTR(batt, 0444, batt_show, NULL);
static DEVICE_ATTR(leakcurr, 0444, leakcurr_show, NULL);
static DEVICE_ATTR(max17320_protstat, 0444, max17320_protstat_show, NULL);
static DEVICE_ATTR(max17330_protstat, 0444, max17330_protstat_show, NULL);
static DEVICE_ATTR(register_store, 0200, NULL, register_store_store);
static DEVICE_ATTR(register_read, 0644, register_read_show, register_read_store);
static DEVICE_ATTR(delay_config, 0644, delay_config_show, delay_config_store);
static DEVICE_ATTR(rsense, 0644, rsense_show, rsense_store);
static DEVICE_ATTR(recovery_voltage, 0644, recovery_voltage_show, recovery_voltage_store);
static DEVICE_ATTR(empty_voltage, 0644, empty_voltage_show, empty_voltage_store);
static DEVICE_ATTR(dOVP, 0644, dOVP_show, dOVP_store);
static DEVICE_ATTR(dOVPR, 0644, dOVPR_show, dOVPR_store);
static DEVICE_ATTR(ovp_perm_fail, 0644, OVP_perm_fail_show, OVP_perm_fail_store);
static DEVICE_ATTR(ChgDetTh, 0644, ChgDetTh_show, ChgDetTh_store);
static DEVICE_ATTR(OCTH, 0644, OCTH_show, OCTH_store);
static DEVICE_ATTR(ODTH, 0644, ODTH_show, ODTH_store);
static DEVICE_ATTR(SCTH, 0644, SCTH_show, SCTH_store);
static DEVICE_ATTR(UVP, 0644, UVP_show, UVP_store);
static DEVICE_ATTR(UOCVP, 0644, UOCVP_show, UOCVP_store);
static DEVICE_ATTR(UVShdn, 0644, UVShdn_show, UVShdn_store);
static DEVICE_ATTR(A1EN, 0644, A1EN_show, A1EN_store);
static DEVICE_ATTR(R100, 0644, R100_show, R100_store);
static DEVICE_ATTR(OCDLY, 0644, OCDLY_show, OCDLY_store);
static DEVICE_ATTR(SCDLY, 0644, SCDLY_show, SCDLY_store);
static DEVICE_ATTR(T1, 0644, T1_show, T1_store);
static DEVICE_ATTR(T2, 0644, T2_show, T2_store);
static DEVICE_ATTR(T3, 0644, T3_show, T3_store);
static DEVICE_ATTR(T4, 0644, T4_show, T4_store);
static DEVICE_ATTR(Twarm, 0644, Twarm_show, Twarm_store);
static DEVICE_ATTR(TpermFailHot, 0644, TpermFailHot_show, TpermFailHot_store);
static DEVICE_ATTR(PreQualVolt, 0644, PreQualVolt_show, PreQualVolt_store);
static DEVICE_ATTR(VSysMin, 0644, VSysMin_show, VSysMin_store);
static DEVICE_ATTR(PreChgCurr, 0644, PreChgCurr_show, PreChgCurr_store);
static DEVICE_ATTR(HeatLim, 0644, HeatLim_show, HeatLim_store);
static DEVICE_ATTR(FETLim, 0644, FETLim_show, FETLim_store);
static DEVICE_ATTR(FetTheta, 0644, FetTheta_show, FetTheta_store);
static DEVICE_ATTR(push_button, 0644, push_button_show, push_button_store);
static DEVICE_ATTR(enJ, 0644, enJ_show, enJ_store);
static DEVICE_ATTR(control, 0200, NULL, control_store);

static struct attribute *max1720x_attr[] = {
	&dev_attr_log.attr,
	&dev_attr_nvmem.attr,
	&dev_attr_atrate.attr,
	&dev_attr_attte.attr,
	&dev_attr_cell.attr,
	&dev_attr_temp.attr,
	&dev_attr_batt.attr,
	NULL};

static struct attribute *max17320_attr[] = {
	&dev_attr_log.attr,
	&dev_attr_nvmem.attr,
	&dev_attr_atrate.attr,
	&dev_attr_attte.attr,
	&dev_attr_cell.attr,
	&dev_attr_temp.attr,
	&dev_attr_batt.attr,
	&dev_attr_max17320_protstat.attr,
	&dev_attr_register_store.attr,
	NULL};

static struct attribute *max17330_attr[] = {
	&dev_attr_log.attr,
	&dev_attr_nvmem.attr,
	&dev_attr_atrate.attr,
	&dev_attr_voltageVal.attr,
	&dev_attr_currentVal.attr,
	&dev_attr_fet.attr,
	&dev_attr_dfet.attr,
	&dev_attr_ship.attr,
	&dev_attr_attte.attr,
	&dev_attr_cell.attr,
	&dev_attr_temp.attr,
	&dev_attr_batt.attr,
	&dev_attr_leakcurr.attr,
	&dev_attr_max17330_protstat.attr,
	&dev_attr_register_store.attr,
	&dev_attr_register_read.attr,
	&dev_attr_delay_config.attr,
	&dev_attr_rsense.attr,
	&dev_attr_recovery_voltage.attr,
	&dev_attr_empty_voltage.attr,
	&dev_attr_dOVP.attr,
	&dev_attr_dOVPR.attr,
	&dev_attr_ovp_perm_fail.attr,
	&dev_attr_ChgDetTh.attr,
	&dev_attr_OCTH.attr,
	&dev_attr_ODTH.attr,
	&dev_attr_SCTH.attr,
	&dev_attr_UVP.attr,
	&dev_attr_UOCVP.attr,
	&dev_attr_UVShdn.attr,
	&dev_attr_A1EN.attr,
	&dev_attr_R100.attr,
	&dev_attr_OCDLY.attr,
	&dev_attr_SCDLY.attr,
	&dev_attr_T1.attr,
	&dev_attr_T2.attr,
	&dev_attr_T3.attr,
	&dev_attr_T4.attr,
	&dev_attr_Twarm.attr,
	&dev_attr_TpermFailHot.attr,
	&dev_attr_PreQualVolt.attr,
	&dev_attr_VSysMin.attr,
	&dev_attr_PreChgCurr.attr,
	&dev_attr_HeatLim.attr,
	&dev_attr_FETLim.attr,
	&dev_attr_FetTheta.attr,
	&dev_attr_push_button.attr,
	&dev_attr_enJ.attr,
	&dev_attr_control.attr,
	NULL};

static struct attribute_group max1720x_attr_group = {
	.attrs = max1720x_attr,
};

static struct attribute_group max17320_attr_group = {
	.attrs = max17320_attr,
};

static struct attribute_group max17330_attr_group = {
	.attrs = max17330_attr,
};

static struct attribute_group *attr_groups[] = {
	[ID_MAX1720X] = &max1720x_attr_group,
	[ID_MAX1730X] = &max1720x_attr_group,
	[ID_MAX17320] = &max17320_attr_group,
	[ID_MAX17330] = &max17330_attr_group,
};

static int max1720x_regmap_read(struct max1720x_priv *priv, unsigned int reg, unsigned int *val)
{
	int ret = 0;
	struct regmap *map = priv->regmap;

	mutex_lock(&priv->lock);
	if ((reg & 0x100) == 0x100) {
		priv->client->addr = NONVOLATILE_DATA_I2C_ADDR;
		ret = regmap_read(map, (reg & 0xFF), val);
		priv->client->addr = MODELGAUGE_DATA_I2C_ADDR;
	} else {
		ret = regmap_read(map, reg, val);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int max1720x_regmap_write(struct max1720x_priv *priv, unsigned int reg, unsigned int val)
{
	int ret = 0;
	u32 data;
	struct regmap *map = priv->regmap;

	mutex_lock(&priv->lock);
	if ((priv->driver_data == ID_MAX17320) || (priv->driver_data == ID_MAX17330)) {

		ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &data);
		if (!(ret < 0)) {
			// Unlock Write Protection
			regmap_write(map, priv->regs[COMMSTTAT_REG], data & 0xFF06);
			regmap_write(map, priv->regs[COMMSTTAT_REG], data & 0xFF06);

			if ((reg & 0x100) == 0x100) {
				priv->client->addr = NONVOLATILE_DATA_I2C_ADDR;
				ret = regmap_write(map, (reg & 0xFF), val);
				priv->client->addr = MODELGAUGE_DATA_I2C_ADDR;
			} else {
				ret = regmap_write(map, reg, val);
			}

			// lock Write Protection
			regmap_write(map, priv->regs[COMMSTTAT_REG], data | 0x00F9);
			regmap_write(map, priv->regs[COMMSTTAT_REG], data | 0x00F9);
		}
	} else {
		if ((reg & 0x100) == 0x100) {
			priv->client->addr = NONVOLATILE_DATA_I2C_ADDR;
			ret = regmap_write(map, reg, val);
			priv->client->addr = MODELGAUGE_DATA_I2C_ADDR;
		} else {
			ret = regmap_write(map, reg, val);
		}
	}

	mutex_unlock(&priv->lock);
	return ret;
}

static int max1720x_get_temperature(struct max1720x_priv *priv, int *temp)
{
	int ret;
	u32 data;

	ret = max1720x_regmap_read(priv, priv->regs[TEMP_REG], &data);
	if (ret < 0)
		return ret;

	*temp = sign_extend32(data, 15);
	/* The value is converted into centigrade scale */
	/* Units of LSB = 1 / 256 degree Celsius */
	*temp = (*temp * 10) >> 8;
	return 0;
}

static int max1720x_set_temp_lower_limit(struct max1720x_priv *priv,
										 int temp)
{
	int ret;
	u32 data;

	ret = max1720x_regmap_read(priv, priv->regs[TALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	/* Input in deci-centigrade, convert to centigrade */
	temp /= 10;

	data &= 0xFF00;
	data |= (temp & 0xFF);

	ret = max1720x_regmap_write(priv, priv->regs[TALRTTH_REG], data);
	if (ret < 0)
		return ret;

	return 0;
}

static int max1720x_get_temperature_alert_min(struct max1720x_priv *priv,
											  int *temp)
{
	int ret;
	u32 data;

	ret = max1720x_regmap_read(priv, priv->regs[TALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	/* Convert 1DegreeC LSB to 0.1DegreeC LSB */
	*temp = sign_extend32(data & 0xff, 7) * 10;

	return 0;
}

static int max1720x_set_temp_upper_limit(struct max1720x_priv *priv,
										 int temp)
{
	int ret;
	u32 data;

	ret = max1720x_regmap_read(priv, priv->regs[TALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	/* Input in deci-centigrade, convert to centigrade */
	temp /= 10;

	data &= 0xFF;
	data |= ((temp << 8) & 0xFF00);

	ret = max1720x_regmap_write(priv, priv->regs[TALRTTH_REG], data);
	if (ret < 0)
		return ret;

	return 0;
}

static int max1720x_get_temperature_alert_max(struct max1720x_priv *priv,
											  int *temp)
{
	int ret;
	u32 data;

	ret = max1720x_regmap_read(priv, priv->regs[TALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	/* Convert 1DegreeC LSB to 0.1DegreeC LSB */
	*temp = sign_extend32(data >> 8, 7) * 10;

	return 0;
}

static int max1720x_get_battery_health(struct max1720x_priv *priv, int *health)
{
	int temp, vavg, vbatt, ret;
	u32 val;

	ret = max1720x_regmap_read(priv, priv->regs[AVGVCELL_REG], &val);
	if (ret < 0)
		goto health_error;

	/* bits [0-3] unused */
	vavg = max1720x_raw_voltage_to_uvolts(priv, val);
	/* Convert to millivolts */
	vavg /= 1000;

	ret = max1720x_regmap_read(priv, priv->regs[VCELL_REG], &val);
	if (ret < 0)
		goto health_error;

	/* bits [0-3] unused */
	vbatt = max1720x_raw_voltage_to_uvolts(priv, val);
	/* Convert to millivolts */
	vbatt /= 1000;

	if (vavg < priv->pdata->volt_min) {
		*health = POWER_SUPPLY_HEALTH_DEAD;
		goto out;
	}

	if (vbatt > priv->pdata->volt_max + MAX1720X_VMAX_TOLERANCE) {
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		goto out;
	}

	ret = max1720x_get_temperature(priv, &temp);
	if (ret < 0)
		goto health_error;

	if (temp <= priv->pdata->temp_min) {
		*health = POWER_SUPPLY_HEALTH_COLD;
		goto out;
	}

	if (temp >= priv->pdata->temp_max) {
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
		goto out;
	}

	*health = POWER_SUPPLY_HEALTH_GOOD;

out:
	return 0;

health_error:
	return ret;
}

static int max1730x_get_battery_health(struct max1720x_priv *priv, int *health)
{
	int ret;
	u32 val;

	ret = max1720x_regmap_read(priv, priv->regs[PROTSTATUS_REG], &val);
	if (ret < 0)
		return ret;

	if ((val & MAX1730X_PROTSTATUS_RESCFAULT) ||
		(val & MAX1730X_PROTSTATUS_RESDFAULT)) {
		*health = POWER_SUPPLY_HEALTH_UNKNOWN;
	} else if ((val & MAX1730X_PROTSTATUS_TOOHOTC) ||
			 (val & MAX1730X_PROTSTATUS_TOOHOTD) ||
			 (val & MAX1730X_PROTSTATUS_DIEHOT)) {
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if ((val & MAX1730X_PROTSTATUS_UVP) ||
			 (val & MAX1730X_PROTSTATUS_PERMFAIL) ||
			 (val & MAX1730X_PROTSTATUS_SHDN)) {
		*health = POWER_SUPPLY_HEALTH_DEAD;
	} else if (val & MAX1730X_PROTSTATUS_TOOCOLDC) {
		*health = POWER_SUPPLY_HEALTH_COLD;
	} else if (val & MAX1730X_PROTSTATUS_OVP) {
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if ((val & MAX1730X_PROTSTATUS_QOVFLW) ||
			 (val & MAX1730X_PROTSTATUS_OCCP) ||
			 (val & MAX1730X_PROTSTATUS_ODCP)) {
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	} else if (val & MAX1730X_PROTSTATUS_CHGWDT) {
		*health = POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;
	} else {
		*health = POWER_SUPPLY_HEALTH_GOOD;
	}

	return 0;
}

static int max17320_get_battery_health(struct max1720x_priv *priv, int *health)
{
	int ret;
	u32 val;

	ret = max1720x_regmap_read(priv, priv->regs[PROTSTATUS_REG], &val);
	if (ret < 0)
		return ret;

	if ((val & MAX17320_PROTSTATUS_RESDFAULT) ||
		(val & MAX17320_PROTSTATUS_PREQF) ||
		(val & MAX17320_PROTSTATUS_IMBALANCE)) {
		*health = POWER_SUPPLY_HEALTH_UNKNOWN;
	} else if ((val & MAX17320_PROTSTATUS_TOOHOTC) ||
			 (val & MAX17320_PROTSTATUS_TOOHOTD) ||
			 (val & MAX17320_PROTSTATUS_DIEHOT)) {
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if ((val & MAX17320_PROTSTATUS_UVP) ||
			 (val & MAX17320_PROTSTATUS_PERMFAIL) ||
			 (val & MAX17320_PROTSTATUS_SHDN)) {
		*health = POWER_SUPPLY_HEALTH_DEAD;
	} else if (val & MAX17320_PROTSTATUS_TOOCOLDC) {
		*health = POWER_SUPPLY_HEALTH_COLD;
	} else if (val & MAX17320_PROTSTATUS_OVP) {
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if ((val & MAX17320_PROTSTATUS_QOVFLW) ||
			 (val & MAX17320_PROTSTATUS_OCCP) ||
			 (val & MAX17320_PROTSTATUS_ODCP)) {
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	} else if (val & MAX17320_PROTSTATUS_CHGWDT) {
		*health = POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;
	} else {
		*health = POWER_SUPPLY_HEALTH_GOOD;
	}

	return 0;
}

static int max17330_get_battery_health(struct max1720x_priv *priv, int *health)
{
	int ret;
	u32 val;

	ret = max1720x_regmap_read(priv, priv->regs[PROTSTATUS_REG], &val);
	if (ret < 0)
		return ret;

	if (val & MAX17330_PROTSTATUS_PREQF) {
		*health = POWER_SUPPLY_HEALTH_UNKNOWN;
	} else if ((val & MAX17330_PROTSTATUS_TOOHOTC) ||
			 (val & MAX17330_PROTSTATUS_TOOHOTD) ||
			 (val & MAX17330_PROTSTATUS_DIEHOT)) {
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if ((val & MAX17330_PROTSTATUS_UVP) ||
			 (val & MAX17330_PROTSTATUS_PERMFAIL) ||
			 (val & MAX17330_PROTSTATUS_SHDN)) {
		*health = POWER_SUPPLY_HEALTH_DEAD;
	} else if ((val & MAX17330_PROTSTATUS_TOOCOLDC) ||
			 (val & MAX17330_PROTSTATUS_TOOCOLDD)) {
		*health = POWER_SUPPLY_HEALTH_COLD;
	} else if (val & MAX17330_PROTSTATUS_OVP) {
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if ((val & MAX17330_PROTSTATUS_QOVFLW) ||
			 (val & MAX17330_PROTSTATUS_OCCP) ||
			 (val & MAX17330_PROTSTATUS_ODCP)) {
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	} else if (val & MAX17330_PROTSTATUS_CHGWDT) {
		*health = POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;
	} else {
		*health = POWER_SUPPLY_HEALTH_GOOD;
	}

	return 0;
}

static int max1720x_get_min_capacity_alert_th(struct max1720x_priv *priv,
											  unsigned int *th)
{
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[SALRTTH_REG], th);
	if (ret < 0)
		return ret;

	*th &= 0xFF;

	return 0;
}

static int max1720x_set_min_capacity_alert_th(struct max1720x_priv *priv,
											  unsigned int th)
{
	int ret;
	unsigned int data;

	ret = max1720x_regmap_read(priv, priv->regs[SALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	data &= 0xFF00;
	data |= (th & 0xFF);

	ret = max1720x_regmap_write(priv, priv->regs[SALRTTH_REG], data);
	if (ret < 0)
		return ret;

	return 0;
}

static int max1720x_get_max_capacity_alert_th(struct max1720x_priv *priv,
											  unsigned int *th)
{
	int ret;

	ret = max1720x_regmap_read(priv, priv->regs[SALRTTH_REG], th);
	if (ret < 0)
		return ret;

	*th >>= 8;

	return 0;
}

static int max1720x_set_max_capacity_alert_th(struct max1720x_priv *priv,
											  unsigned int th)
{
	int ret;
	unsigned int data;

	ret = max1720x_regmap_read(priv, priv->regs[SALRTTH_REG], &data);
	if (ret < 0)
		return ret;

	data &= 0xFF;
	data |= ((th & 0xFF) << 8);

	ret = max1720x_regmap_write(priv, priv->regs[SALRTTH_REG], data);
	if (ret < 0)
		return ret;

	return 0;
}

static int max1720x_get_property(struct power_supply *psy,
								 enum power_supply_property psp,
								 union power_supply_propval *val)
{
	struct max1720x_priv *priv = power_supply_get_drvdata(psy);
	struct max1720x_platform_data *pdata = priv->pdata;
	unsigned int reg;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		ret = max1720x_regmap_read(priv, priv->regs[STATUS_REG], &reg);
		if (ret < 0)
			return ret;
		if (reg & MAX1720X_STATUS_BST)
			val->intval = 0;
		else
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = max1720x_regmap_read(priv, priv->regs[CYCLES_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = reg * priv->cycles_reg_lsb_percent / 100;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		ret = max1720x_regmap_read(priv, priv->regs[MAXMINVOLT_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = reg >> 8;
		val->intval *= 20000; /* Units of LSB = 20mV */
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		ret = max1720x_regmap_read(priv, priv->regs[VEMPTY_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = reg >> 7;
		val->intval *= 10000; /* Units of LSB = 10mV */
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (pdata && priv->get_charging_status)
			val->intval = priv->get_charging_status();
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = max1720x_regmap_read(priv, priv->regs[VCELL_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_voltage_to_uvolts(priv, reg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = max1720x_regmap_read(priv, priv->regs[AVGVCELL_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_voltage_to_uvolts(priv, reg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		ret = max1720x_regmap_read(priv, priv->regs[VFOCV_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_voltage_to_uvolts(priv, reg);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = max1720x_regmap_read(priv, priv->regs[REPSOC_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = reg >> 8; /* RepSOC LSB: 1/256 % */
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		ret = max1720x_get_min_capacity_alert_th(priv, &val->intval);
		if (ret < 0)
			return ret;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX:
		ret = max1720x_get_max_capacity_alert_th(priv, &val->intval);
		if (ret < 0)
			return ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = max1720x_regmap_read(priv, priv->regs[DESIGNCAP_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_capacity_to_uamph(priv, reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = max1720x_regmap_read(priv, priv->regs[FULLCAPREP_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_capacity_to_uamph(priv, reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = max1720x_regmap_read(priv, priv->regs[QH_REG], &reg);
		if (ret < 0)
			return ret;
		/* This register is signed as oppose to other capacity type
		 * registers.
		 */
		val->intval = max1720x_raw_capacity_to_uamph(priv,
													 sign_extend32(reg, 15));
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = max1720x_regmap_read(priv, priv->regs[REPCAP_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_capacity_to_uamph(priv, reg);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = max1720x_get_temperature(priv, &val->intval);
		if (ret < 0)
			return ret;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		ret = max1720x_get_temperature_alert_min(priv, &val->intval);
		if (ret < 0)
			return ret;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = max1720x_get_temperature_alert_max(priv, &val->intval);
		if (ret < 0)
			return ret;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (priv->get_battery_health != 0) {
			ret = priv->get_battery_health(priv, &val->intval);
			if (ret < 0)
				return ret;
		} else {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = max1720x_regmap_read(priv, priv->regs[CURRENT_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_current_to_uamps(priv, sign_extend32(reg, 15));
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = max1720x_regmap_read(priv, priv->regs[AVGCURRENT_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = max1720x_raw_current_to_uamps(priv, sign_extend32(reg, 15));
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = max1720x_regmap_read(priv, priv->regs[TTE_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = (reg * 45) >> 3; /* TTE LSB: 5.625 sec */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = max1720x_regmap_read(priv, priv->regs[TTF_REG], &reg);
		if (ret < 0)
			return ret;
		val->intval = (reg * 45) >> 3; /* TTF LSB: 5.625 sec */
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int max1720x_set_property(struct power_supply *psy,
								 enum power_supply_property psp,
								 const union power_supply_propval *val)
{
	struct max1720x_priv *priv = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		ret = max1720x_set_temp_lower_limit(priv, val->intval);
		if (ret < 0)
			dev_err(priv->dev, "temp alert min set fail:%d\n",
					ret);
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = max1720x_set_temp_upper_limit(priv, val->intval);
		if (ret < 0)
			dev_err(priv->dev, "temp alert max set fail:%d\n",
					ret);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		ret = max1720x_set_min_capacity_alert_th(priv, val->intval);
		if (ret < 0)
			dev_err(priv->dev, "capacity alert min set fail:%d\n",
					ret);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX:
		ret = max1720x_set_max_capacity_alert_th(priv, val->intval);
		if (ret < 0)
			dev_err(priv->dev, "capacity alert max set fail:%d\n",
					ret);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int max1720x_property_is_writeable(struct power_supply *psy,
										  enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static irqreturn_t max1720x_irq_handler(int id, void *dev)
{
	struct max1720x_priv *priv = dev;
	u32 val;

	/* Check alert type */
	max1720x_regmap_read(priv, priv->regs[STATUS_REG], &val);

	if (val & MAX1720X_STATUS_SOC_MAX_ALRT)
		dev_info(priv->dev, "Alert: SOC MAX!\n");
	if (val & MAX1720X_STATUS_SOC_MIN_ALRT)
		dev_info(priv->dev, "Alert: SOC MIN!\n");
	if (val & MAX1720X_STATUS_TEMP_MAX_ALRT)
		dev_info(priv->dev, "Alert: TEMP MAX!\n");
	if (val & MAX1720X_STATUS_TEMP_MIN_ALRT)
		dev_info(priv->dev, "Alert: TEMP MIN!\n");
	if (val & MAX1720X_STATUS_VOLT_MAX_ALRT)
		dev_info(priv->dev, "Alert: VOLT MAX!\n");
	if (val & MAX1720X_STATUS_VOLT_MIN_ALRT)
		dev_info(priv->dev, "Alert: VOLT MIN!\n");
	if (val & MAX1720X_STATUS_CURR_MAX_ALRT)
		dev_info(priv->dev, "Alert: CURR MAX!\n");
	if (val & MAX1720X_STATUS_CURR_MIN_ALRT)
		dev_info(priv->dev, "Alert: CURR MIN!\n");

	/* Clear alerts */
	max1720x_regmap_write(priv, priv->regs[STATUS_REG],
				 val & MAX1720X_STATUS_ALRT_CLR_MASK);

	power_supply_changed(priv->battery);

	return IRQ_HANDLED;
}

static irqreturn_t max17320_irq_handler(int id, void *dev)
{
	struct max1720x_priv *priv = dev;
	u32 val;

	/* Check alert type */
	max1720x_regmap_read(priv, priv->regs[STATUS_REG], &val);

	if (val & MAX1720X_STATUS_PROTECTION_ALRT) {
		dev_info(priv->dev, "Alert: Protection!\n");

		/* Check Protection Alert type */
		max1720x_regmap_read(priv, priv->regs[PROTALRTS_REG], &val);

		if (val & MAX17320_PROTSTATUS_CHGWDT)
			dev_info(priv->dev, "Protection Alert: Charge Watch Dog Timer!\n");
		if (val & MAX17320_PROTSTATUS_TOOHOTC)
			dev_info(priv->dev, "Protection Alert: Overtemperature for Charging!\n");
		if (val & MAX17320_PROTSTATUS_FULL)
			dev_info(priv->dev, "Protection Alert: Full Detection!\n");
		if (val & MAX17320_PROTSTATUS_TOOCOLDC)
			dev_info(priv->dev, "Protection Alert: Undertemperature!\n");
		if (val & MAX17320_PROTSTATUS_OVP)
			dev_info(priv->dev, "Protection Alert: Overvoltage!\n");
		if (val & MAX17320_PROTSTATUS_OCCP)
			dev_info(priv->dev, "Protection Alert: Overcharge Current!\n");
		if (val & MAX17320_PROTSTATUS_QOVFLW)
			dev_info(priv->dev, "Protection Alert: Q Overflow!\n");
		if (val & MAX17320_PROTSTATUS_PREQF)
			dev_info(priv->dev, "Protection Alert: Prequal timeout!\n");
		if (val & MAX17320_PROTSTATUS_IMBALANCE)
			dev_info(priv->dev, "Protection Alert: Multi-cell imbalance!\n");
		if (val & MAX17320_PROTSTATUS_PERMFAIL)
			dev_info(priv->dev, "Protection Alert: Permanent Failure!\n");
		if (val & MAX17320_PROTSTATUS_DIEHOT)
			dev_info(priv->dev, "Protection Alert: Overtemperature for die temperature!\n");
		if (val & MAX17320_PROTSTATUS_TOOHOTD)
			dev_info(priv->dev, "Protection Alert: Overtemperature for Discharging!\n");
		if (val & MAX17320_PROTSTATUS_UVP)
			dev_info(priv->dev, "Protection Alert: Undervoltage Protection!\n");
		if (val & MAX17320_PROTSTATUS_ODCP)
			dev_info(priv->dev, "Protection Alert: Overdischarge current!\n");
		if (val & MAX17320_PROTSTATUS_RESDFAULT)
			dev_info(priv->dev, "Protection Alert: Discharging Fault!\n");
		if (val & MAX17320_PROTSTATUS_SHDN)
			dev_info(priv->dev, "Protection Alert: Shutdown Event !\n");
	}

	if (val & MAX1720X_STATUS_SOC_MAX_ALRT)
		dev_info(priv->dev, "Alert: SOC MAX!\n");
	if (val & MAX1720X_STATUS_SOC_MIN_ALRT)
		dev_info(priv->dev, "Alert: SOC MIN!\n");
	if (val & MAX1720X_STATUS_TEMP_MAX_ALRT)
		dev_info(priv->dev, "Alert: TEMP MAX!\n");
	if (val & MAX1720X_STATUS_TEMP_MIN_ALRT)
		dev_info(priv->dev, "Alert: TEMP MIN!\n");
	if (val & MAX1720X_STATUS_VOLT_MAX_ALRT)
		dev_info(priv->dev, "Alert: VOLT MAX!\n");
	if (val & MAX1720X_STATUS_VOLT_MIN_ALRT)
		dev_info(priv->dev, "Alert: VOLT MIN!\n");
	if (val & MAX1720X_STATUS_CURR_MAX_ALRT)
		dev_info(priv->dev, "Alert: CURR MAX!\n");
	if (val & MAX1720X_STATUS_CURR_MIN_ALRT)
		dev_info(priv->dev, "Alert: CURR MIN!\n");

	/* Clear alerts */
	max1720x_regmap_write(priv, priv->regs[STATUS_REG],
									  val & MAX17320_STATUS_ALRT_CLR_MASK);

	power_supply_changed(priv->battery);

	return IRQ_HANDLED;
}

static irqreturn_t max17330_irq_handler(int id, void *dev)
{
	struct max1720x_priv *priv = dev;
	u32 val;

	/* Check alert type */
	max1720x_regmap_read(priv, priv->regs[STATUS_REG], &val);

	if (val & MAX1720X_STATUS_PROTECTION_ALRT) {
		dev_info(priv->dev, "Alert: Protection!\n");

		/* Check Protection Alert type */
		max1720x_regmap_read(priv, priv->regs[PROTALRTS_REG], &val);

		if (val & MAX17330_PROTSTATUS_CHGWDT)
			dev_info(priv->dev, "Protection Alert: Charge Watch Dog Timer!\n");
		if (val & MAX17330_PROTSTATUS_TOOHOTC)
			dev_info(priv->dev, "Protection Alert: Overtemperature for Charging!\n");
		if (val & MAX17330_PROTSTATUS_FULL)
			dev_info(priv->dev, "Protection Alert: Full Detection!\n");
		if (val & MAX17330_PROTSTATUS_TOOCOLDC)
			dev_info(priv->dev, "Protection Alert: Undertemperature!\n");
		if (val & MAX17330_PROTSTATUS_OVP)
			dev_info(priv->dev, "Protection Alert: Overvoltage!\n");
		if (val & MAX17330_PROTSTATUS_OCCP)
			dev_info(priv->dev, "Protection Alert: Overcharge Current!\n");
		if (val & MAX17330_PROTSTATUS_QOVFLW)
			dev_info(priv->dev, "Protection Alert: Q Overflow!\n");
		if (val & MAX17330_PROTSTATUS_PREQF)
			dev_info(priv->dev, "Protection Alert: Prequal timeout!\n");
		if (val & MAX17330_PROTSTATUS_BLOCKCHG)
			dev_info(priv->dev, "Protection Alert: Block change!\n");
		if (val & MAX17330_PROTSTATUS_PERMFAIL)
			dev_info(priv->dev, "Protection Alert: Permanent Failure!\n");
		if (val & MAX17330_PROTSTATUS_DIEHOT)
			dev_info(priv->dev, "Protection Alert: Overtemperature for die temperature!\n");
		if (val & MAX17330_PROTSTATUS_TOOHOTD)
			dev_info(priv->dev, "Protection Alert: Overtemperature for Discharging!\n");
		if (val & MAX17330_PROTSTATUS_UVP)
			dev_info(priv->dev, "Protection Alert: Undervoltage Protection!\n");
		if (val & MAX17330_PROTSTATUS_ODCP)
			dev_info(priv->dev, "Protection Alert: Overdischarge current!\n");
		if (val & MAX17330_PROTSTATUS_TOOCOLDD)
			dev_info(priv->dev, "Protection Alert: Undertemperature for Discharging!\n");
		if (val & MAX17330_PROTSTATUS_SHDN)
			dev_info(priv->dev, "Protection Alert: Shutdown Event !\n");
	}

	if (val & MAX1720X_STATUS_SOC_MAX_ALRT)
		dev_info(priv->dev, "Alert: SOC MAX!\n");
	if (val & MAX1720X_STATUS_SOC_MIN_ALRT)
		dev_info(priv->dev, "Alert: SOC MIN!\n");
	if (val & MAX1720X_STATUS_TEMP_MAX_ALRT)
		dev_info(priv->dev, "Alert: TEMP MAX!\n");
	if (val & MAX1720X_STATUS_TEMP_MIN_ALRT)
		dev_info(priv->dev, "Alert: TEMP MIN!\n");
	if (val & MAX1720X_STATUS_VOLT_MAX_ALRT)
		dev_info(priv->dev, "Alert: VOLT MAX!\n");
	if (val & MAX1720X_STATUS_VOLT_MIN_ALRT)
		dev_info(priv->dev, "Alert: VOLT MIN!\n");
	if (val & MAX1720X_STATUS_CURR_MAX_ALRT)
		dev_info(priv->dev, "Alert: CURR MAX!\n");
	if (val & MAX1720X_STATUS_CURR_MIN_ALRT)
		dev_info(priv->dev, "Alert: CURR MIN!\n");

	if (val & MAX1720X_STATUS_CHARGING_ALRT) {
		dev_info(priv->dev, "Alert: CHARGING!\n");

		/* Check Charging type */
		max1720x_regmap_read(priv, priv->regs[CHGSTAT_REG], &val);

		if (val & MAX17330_CHGSTAT_CP)
			dev_info(priv->dev, "Charging Alert: Heat limit!\n");
		if (val & MAX17330_CHGSTAT_CT)
			dev_info(priv->dev, "Charging Alert: FET Temperature limit!\n");
		if (val & MAX17330_CHGSTAT_DROPOUT)
			dev_info(priv->dev, "Charging Alert: Dropout!\n");
	}

	/* Clear alerts */
	max1720x_regmap_write(priv, priv->regs[STATUS_REG],
									  val & MAX17330_STATUS_ALRT_CLR_MASK);

	power_supply_changed(priv->battery);

	return IRQ_HANDLED;
}

static void max1720x_set_alert_thresholds(struct max1720x_priv *priv)
{
	struct max1720x_platform_data *pdata = priv->pdata;
	u32 val;

	/* Set VAlrtTh */
	val = (pdata->volt_min / 20);
	val |= ((pdata->volt_max / 20) << 8);
	max1720x_regmap_write(priv, priv->regs[VALRTTH_REG], val);

	/* Set TAlrtTh */
	val = pdata->temp_min & 0xFF;
	val |= ((pdata->temp_max & 0xFF) << 8);
	max1720x_regmap_write(priv, priv->regs[TALRTTH_REG], val);

	/* Set SAlrtTh */
	val = pdata->soc_min;
	val |= (pdata->soc_max << 8);
	max1720x_regmap_write(priv, priv->regs[SALRTTH_REG], val);

	/* Set IAlrtTh */
	val = (pdata->curr_min * pdata->rsense / 400) & 0xFF;
	val |= (((pdata->curr_max * pdata->rsense / 400) & 0xFF) << 8);
	max1720x_regmap_write(priv, priv->regs[IALRTTH_REG], val);
}

static int max1720x_init(struct max1720x_priv *priv)
{
	int ret;
	int reg_ctr = 0;
	unsigned int reg;
	u32 data;
	u32 fgrev;

	u16 max173xx_nvm_defaults[][2] = {
		{priv->regs[NICHGTERM_REG], 0x01E0},
		{priv->regs[NRCOMP0_REG], 0x04B8},
		{priv->regs[NPACKCFG_REG], 0x0000},
		{priv->regs[NVCFG1_REG], 0x2102},
		{priv->regs[NFULLSOCTHR_REG], 0x5005},
		{priv->regs[NCHGCFG1_REG], 0x3FFF},
		{priv->regs[NRSENSE_REG], 0x01F4},
		{priv->regs[NVPRTTH1_REG], 0x0008},
		{priv->regs[NTPRTTH1_REG], 0x5000},
		{priv->regs[NPROTMISCTH_REG], 0x7A5F},
		{priv->regs[NPROTCFG_REG], 0x3408},
		{priv->regs[NJEITAC_REG], 0x8BFF},
		{priv->regs[NJEITAV_REG], 0x2800},
		{priv->regs[NOVPRTTH_REG], 0xF3F4},
		{priv->regs[NSTEPCHG_REG], 0xFF00},
		{priv->regs[NDELAYCFG_REG], 0x1B3D},
		{priv->regs[NCHECKSUM_REG], 0x1065},
	};

	ret = max1720x_regmap_read(priv, priv->regs[VERSION_REG], &fgrev);
	if (ret < 0)
		return ret;

	dev_info(priv->dev, "IC Version: 0x%04x\n", fgrev);

	/* Optional step - alert threshold initialization */
	max1720x_set_alert_thresholds(priv);

	/* Clear Status.POR */
	ret = max1720x_regmap_read(priv, priv->regs[STATUS_REG], &reg);
	if (ret < 0)
		return ret;

	ret = max1720x_regmap_write(priv, priv->regs[STATUS_REG],
											reg & ~MAX1720X_STATUS_POR);
	if (ret < 0)
		return ret;

	if (priv->driver_data == ID_MAX17330) {
		ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &data);
		if (!(ret < 0)) {
			// Unlock Write Protection
			regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data & 0xFF06);
			regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data & 0xFF06);

			priv->client->addr = NONVOLATILE_DATA_I2C_ADDR;
			for (reg_ctr = 0; reg_ctr < ARRAY_SIZE(max173xx_nvm_defaults); reg_ctr++)
				regmap_write(priv->regmap, (max173xx_nvm_defaults[reg_ctr][0] & 0xFF), max173xx_nvm_defaults[reg_ctr][1]);
			priv->client->addr = MODELGAUGE_DATA_I2C_ADDR;

			regmap_update_bits(priv->regmap, priv->regs[CONFIG2_REG],
								MAX1720X_CONFIG2_POR_CMD,
								MAX1720X_CONFIG2_POR_CMD);

			// Wait 500 ms for POR_CMD to clear;
			mdelay(500);

			// lock Write Protection
			regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data | 0x00F9);
			regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data | 0x00F9);
		}
	}

	return 0;
}

static void max1720x_init_worker(struct work_struct *work)
{
	struct max1720x_priv *priv = container_of(work,
											  struct max1720x_priv,
											  init_worker);

	max1720x_init(priv);
}

static struct max1720x_platform_data *max1720x_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct max1720x_platform_data *pdata;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	ret = of_property_read_u32(np, "talrt-min", &pdata->temp_min);
	if (ret < 0)
		pdata->temp_min = -128; /* DegreeC */ /* Disable alert */

	ret = of_property_read_u32(np, "talrt-max", &pdata->temp_max);
	if (ret < 0)
		pdata->temp_max = 127; /* DegreeC */ /* Disable alert */

	ret = of_property_read_u32(np, "valrt-min", &pdata->volt_min);
	if (ret < 0)
		pdata->volt_min = 0; /* mV */ /* Disable alert */

	ret = of_property_read_u32(np, "valrt-max", &pdata->volt_max);
	if (ret < 0)
		pdata->volt_max = 5100; /* mV */ /* Disable alert */

	ret = of_property_read_u32(np, "ialrt-min", &pdata->curr_min);
	if (ret < 0)
		pdata->curr_min = -5120; /* mA */ /* Disable alert */

	ret = of_property_read_u32(np, "ialrt-max", &pdata->curr_max);
	if (ret < 0)
		pdata->curr_max = 5080; /* mA */ /* Disable alert */

	ret = of_property_read_u32(np, "salrt-min", &pdata->soc_min);
	if (ret < 0)
		pdata->soc_min = 0; /* Percent */ /* Disable alert */

	ret = of_property_read_u32(np, "salrt-max", &pdata->soc_max);
	if (ret < 0)
		pdata->soc_max = 255; /* Percent */ /* Disable alert */

	ret = of_property_read_u32(np, "rsense", &pdata->rsense);
	if (ret < 0)
		pdata->rsense = 10;

	return pdata;
}

static const struct regmap_config max1720x_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static const struct power_supply_desc max1720x_fg_desc = {
	.name = "max1720x_battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = max1720x_battery_props,
	.num_properties = ARRAY_SIZE(max1720x_battery_props),
	.get_property = max1720x_get_property,
	.set_property = max1720x_set_property,
	.property_is_writeable = max1720x_property_is_writeable,
};

static int max1720x_probe(struct i2c_client *client,
						  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct max1720x_priv *priv;
	struct power_supply_config psy_cfg = {};
	int ret;
	u32 data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = chip_regs[id->driver_data];
	priv->nvmem_high_addr = nvmem_high_addrs[id->driver_data];
	priv->cycles_reg_lsb_percent = cycles_reg_lsb_percents[id->driver_data];
	priv->get_battery_health = get_battery_health_handlers[id->driver_data];
	register_to_read = 0x180; /* First register of nvm */
	priv->driver_data = id->driver_data;

	if (client->dev.of_node)
		priv->pdata = max1720x_parse_dt(&client->dev);
	else
		priv->pdata = client->dev.platform_data;

	priv->dev = &client->dev;

	mutex_init(&priv->lock);
	i2c_set_clientdata(client, priv);

	priv->client = client;
	priv->regmap = devm_regmap_init_i2c(client, &max1720x_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	INIT_WORK(&priv->init_worker, max1720x_init_worker);
	schedule_work(&priv->init_worker);

	psy_cfg.drv_data = priv;
	priv->battery = power_supply_register(&client->dev,
										  &max1720x_fg_desc, &psy_cfg);
	if (IS_ERR(priv->battery)) {
		ret = PTR_ERR(priv->battery);
		dev_err(&client->dev, "failed to register battery: %d\n", ret);
		goto err_supply;
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(priv->dev, client->irq,
										NULL,
										irq_handlers[id->driver_data],
										IRQF_TRIGGER_FALLING |
											IRQF_ONESHOT,
										priv->battery->desc->name,
										priv);
		if (ret < 0)  {
			dev_err(priv->dev, "Failed to request irq %d\n",
					client->irq);
			goto err_irq;
		} else {

			if ((id->driver_data == ID_MAX17320) || (id->driver_data == ID_MAX17330)) {
				ret = regmap_read(priv->regmap, priv->regs[COMMSTTAT_REG], &data);
				if (!(ret < 0)) {
					// Unlock Write Protection
					regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data & 0xFF06);
					regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data & 0xFF06);

					regmap_update_bits(priv->regmap, priv->regs[CONFIG_REG],
									MAX1720X_CONFIG_ALRT_EN,
									MAX1720X_CONFIG_ALRT_EN);

					// lock Write Protection
					regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data | 0x00F9);
					regmap_write(priv->regmap, priv->regs[COMMSTTAT_REG], data | 0x00F9);
				}
			}  else {
				regmap_update_bits(priv->regmap, priv->regs[CONFIG_REG],
								   MAX1720X_CONFIG_ALRT_EN,
								   MAX1720X_CONFIG_ALRT_EN);
			}
		}
	}

	/* Create max1720x sysfs attributes */
	priv->attr_grp = attr_groups[id->driver_data];
	ret = sysfs_create_group(&priv->dev->kobj, priv->attr_grp);
	if (ret < 0) {
		dev_err(priv->dev, "Failed to create attribute group [%d]\n",
				ret);
		priv->attr_grp = NULL;
		goto err_attr;
	}

	return 0;

err_irq:
	power_supply_unregister(priv->battery);
err_supply:
	cancel_work_sync(&priv->init_worker);
err_attr:
	sysfs_remove_group(&priv->dev->kobj, priv->attr_grp);
	return ret;
}

static int max1720x_remove(struct i2c_client *client)
{
	struct max1720x_priv *priv = i2c_get_clientdata(client);

	cancel_work_sync(&priv->init_worker);
	sysfs_remove_group(&priv->dev->kobj, priv->attr_grp);
	power_supply_unregister(priv->battery);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max1720x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq) {
		disable_irq(client->irq);
		enable_irq_wake(client->irq);
	}

	return 0;
}

static int max1720x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq) {
		disable_irq_wake(client->irq);
		enable_irq(client->irq);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(max1720x_pm_ops, max1720x_suspend, max1720x_resume);
#define MAX1720X_PM_OPS (&max1720x_pm_ops)
#else
#define MAX1720X_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_OF
static const struct of_device_id max1720x_match[] = {
	{
		.compatible = "maxim,max17201",
	},
	{
		.compatible = "maxim,max17205",
	},
	{
		.compatible = "maxim,max17301",
	},
	{
		.compatible = "maxim,max17302",
	},
	{
		.compatible = "maxim,max17303",
	},
	{
		.compatible = "maxim,max17320",
	},
	{
		.compatible = "maxim,max17330",
	},
	{},
};
MODULE_DEVICE_TABLE(of, max1720x_match);
#endif

static const struct i2c_device_id max1720x_id[] = {
	{"max17201", ID_MAX1720X},
	{"max17205", ID_MAX1720X},
	{"max17301", ID_MAX1730X},
	{"max17302", ID_MAX1730X},
	{"max17303", ID_MAX1730X},
	{"max17320", ID_MAX17320},
	{"max17330", ID_MAX17330},
	{},
};
MODULE_DEVICE_TABLE(i2c, max1720x_id);

static struct i2c_driver max1720x_i2c_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(max1720x_match),
		.pm = MAX1720X_PM_OPS,
	},
	.probe = max1720x_probe,
	.remove = max1720x_remove,
	.id_table = max1720x_id,
};
module_i2c_driver(max1720x_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("opensource@maximintegrated.com");
MODULE_DESCRIPTION("Maxim MAX17201/5, MAX17301/2/3, MAX17320 and MAX17330 Fuel Gauge driver");
