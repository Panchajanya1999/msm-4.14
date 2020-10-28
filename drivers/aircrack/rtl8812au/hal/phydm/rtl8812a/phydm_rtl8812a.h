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

#ifndef __ODM_RTL8812A_H__
#define __ODM_RTL8812A_H__

s8 phydm_cck_rssi_8812a(struct dm_struct *dm, u16 lna_idx, u8 vga_idx);

#ifdef DYN_ANT_WEIGHTING_SUPPORT
void phydm_dynamic_ant_weighting_8812a(void *dm_void);
#endif

void phydm_hwsetting_8812a(struct dm_struct *dm);

#endif
