/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#ifndef __HAL_PHY_RF_8188E_H__
#define __HAL_PHY_RF_8188E_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_88E		15		/* ms */
#define	IQK_DELAY_TIME_8723B		10		/* ms */

#define	index_mapping_NUM_88E	15
#define AVG_THERMAL_NUM_88E	4

#if RT_PLATFORM == PLATFORM_MACOSX
    #include "halphyrf_win.h"
#else
#include "halrf/halphyrf_win.h"
#endif

void configure_txpower_track_8188e(
	struct txpwrtrack_cfg	*config
);

void
get_delta_swing_table_8188e(
	void		*dm_void,
	u8 **temperature_up_a,
	u8 **temperature_down_a,
	u8 **temperature_up_b,
	u8 **temperature_down_b
);

void do_iqk_8188e(
	void		*dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
);

void
odm_tx_pwr_track_set_pwr88_e(
	void				*dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
);

/* 1 7.	IQK */

void
phy_iq_calibrate_8188e(
	void		*dm_void,
	boolean	is_recovery);


/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8188e(
	void				*dm_void
);

/*
 * AP calibrate
 *   */
#if 0 
void
phy_ap_calibrate_8188e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm,
#else
	void	*adapter,
#endif
	s8		delta);
void
phy_digital_predistortion_8188e(void	*adapter);

#endif

#define phy_dp_calibrate_8821a	phy_dp_calibrate_8812a

void
_phy_save_adda_registers(
	struct dm_struct		*dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
);

void
_phy_path_adda_on(
	struct dm_struct		*dm,
	u32		*adda_reg,
	boolean		is_path_a_on,
	boolean		is2T
);

void
_phy_mac_setting_calibration(
	struct dm_struct		*dm,
	u32		*mac_reg,
	u32		*mac_backup
);


void
_phy_path_a_stand_by(
	struct dm_struct		*dm
);


void phy_set_rf_path_switch_8188e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm,
#else
	void	*adapter,
#endif
	boolean		is_main
);

void
halrf_rf_lna_setting_8188e(
	struct dm_struct	*dm,
	enum phydm_lna_set type
);

#endif	/*  #ifndef __HAL_PHY_RF_8188E_H__ */
