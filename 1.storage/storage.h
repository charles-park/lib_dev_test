//------------------------------------------------------------------------------
/**
 * @file storage.h
 * @author charles-park (charles.park@hardkernel.com)
 * @brief Device Test library for ODROID-JIG.
 * @version 2.0
 * @date 2024-11-19
 *
 * @package apt install iperf3, nmap, ethtool, usbutils, alsa-utils
 *
 * @copyright Copyright (c) 2022
 *
 */
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#ifndef __STORAGE_H__
#define __STORAGE_H__

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Define the Device ID for the STORAGE group.
//------------------------------------------------------------------------------
enum {
    // eMMC
    eSTORAGE_eMMC,
    // uSD
    eSTORAGE_uSD,
    // SATA
    eSTORAGE_SATA,
    // NVME
    eSTORAGE_NVME,

    eSTORAGE_END
};

//------------------------------------------------------------------------------
// function prototype
//------------------------------------------------------------------------------
extern int  storage_check       (int dev_id, char *resp);
extern void storage_grp_init    (char *cfg);

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#endif  // #define __STORAGE_H__
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
