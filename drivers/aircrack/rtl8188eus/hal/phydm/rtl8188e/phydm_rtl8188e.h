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
#ifndef	__ODM_RTL8188E_H__
#define __ODM_RTL8188E_H__

#if (RTL8188E_SUPPORT == 1)

void
odm_dig_lower_bound_88e(
	struct dm_struct		*dm
);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

#define sw_ant_div_reset_before_link		odm_sw_ant_div_reset_before_link

void odm_sw_ant_div_reset_before_link(struct dm_struct	*dm);

#endif
#endif
#endif