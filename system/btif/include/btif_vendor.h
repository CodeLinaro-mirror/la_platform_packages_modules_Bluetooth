
/******************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

/************************************************************************************
 *
 *  Filename:      btif_vendor.h
 *
 *  Description:   Vendor Bluetooth Interface
 *
 *
 ***********************************************************************************/

#ifndef BTIF_VENDOR_H
#define BTIF_VENDOR_H

/*******************************************************************************
 *  BTIF VENDOR API
 ******************************************************************************/

/*******************************************************************************
 *
 * Function         btif_vendor_update_add_on_features
 *
 * Description      Update add on feature to app layer
 *
 * Returns          None
 *
 ******************************************************************************/
void btif_vendor_update_add_on_features();

/*******************************************************************************
 *
 * Function         btif_vendor_update_ssr_event
 *
 * Description      Update SSR event to app layer
 *
 * Returns          None
 *
 ******************************************************************************/
void btif_vendor_update_ssr_event();

#endif /* BTIF_VENDOR_H */
