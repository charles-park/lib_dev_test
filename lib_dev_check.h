//------------------------------------------------------------------------------
/**
 * @file lib_dev_test.h
 * @author charles-park (charles.park@hardkernel.com)
 * @brief Device Test library for ODROID-JIG.
 * @version 0.2
 * @date 2023-10-12
 *
 * @package apt install iperf3, nmap, ethtool, usbutils, alsa-utils
 *
 * @copyright Copyright (c) 2022
 *
 */
//------------------------------------------------------------------------------
#ifndef __LIB_DEV_TEST_H__
#define __LIB_DEV_TEST_H__

//------------------------------------------------------------------------------
#include "0.system/system.h"
#include "1.storage/storage.h"
#include "2.usb/usb.h"
#include "3.hdmi/hdmi.h"
#include "4.adc/adc.h"
#include "5.ethernet/ethernet.h"
#include "6.header/header.h"
#include "7.audio/audio.h"
#include "8.led/led.h"
#include "9.pwm/pwm.h"
#include "10.ir/ir.h"
#include "11.gpio/gpio_pin.h"
#include "12.firmware/fw.h"

//------------------------------------------------------------------------------
#define CONFIG_FILE_PATH    "/boot/"

//------------------------------------------------------------------------------
#define STR_PATH_LENGTH     128
#define STR_NAME_LENGTH     16

//------------------------------------------------------------------------------
#define SIZE_UI_ID      4
#define SIZE_GRP_ID     2
#define SIZE_DEV_ID     3
#define SIZE_EXTRA      6

struct msg_info {
    char    start;
    char    cmd;
    char    ui_id [SIZE_UI_ID];
    char    grp_id[SIZE_GRP_ID];
    char    dev_id[SIZE_DEV_ID];
    char    action;
    // extra data (response delay or mac write)
    char    extra [SIZE_EXTRA];
    char    end;
}   __attribute__((packed));

//------------------------------------------------------------------------------
// https://docs.google.com/spreadsheets/d/1igBObU7CnP6FRaRt-x46l5R77-8uAKEskkhthnFwtpY/edit?gid=719914769#gid=719914769
//------------------------------------------------------------------------------
// DEVICE_ACTION Value
// 0 (10 > did) = Read, Clear, PT0
// 1 (20 > did) = Write, Set, PT1
// 2 (30 > did) = Link, PT2
// 3 (40 > did) = PT3
//------------------------------------------------------------------------------
#define DEVICE_ACTION(did)      (did / 10)
#define DEVICE_ID(did)          (did % 10)

//------------------------------------------------------------------------------
// DEVICE_ACTION GPIO Value (GPIO NUM : 0 ~ 999)
// 0 (1000 > did) = Clear
// 1 (2000 > did) = Set
//------------------------------------------------------------------------------
#define DEVICE_ACTION_GPIO(did)  (did / 1000)

//------------------------------------------------------------------------------
//
// message discription
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// start |,|cmd|,|GID|,|DID |,| status |,| value(%20s) |,| end | extra  |
//------------------------------------------------------------------------------
//   1    1  1  1  2  1  4   1     1    1       20      1   1      2      = 36bytes(add extra 38)
//------------------------------------------------------------------------------
//   @   |,| S |,| 00|,|0000|,|P/F/I/W |,|  resp data  |,|  #  | '\r\n' |
//------------------------------------------------------------------------------
#define SERIAL_RESP_SIZE    38
#define SERIAL_RESP_FORM(buf, gid, did, resp)  sprintf (buf, "@,S,%02d,%04d,%s,#\r\n", gid, did, resp)

//#define DEVICE_RESP_SIZE    30
#define DEVICE_RESP_SIZE    22
#define DEVICE_RESP_FORM_INT(buf, status, value) sprintf (buf, "%c,%20d", status, value)
#define DEVICE_RESP_FORM_STR(buf, status, value) sprintf (buf, "%c,%20s", status, value)

//------------------------------------------------------------------------------
// Group ID
//------------------------------------------------------------------------------
enum {
    eGID_SYSTEM = 0,
    eGID_STORAGE,
    eGID_USB,
    eGID_HDMI,
    eGID_ADC,
    eGID_ETHERNET,
    eGID_HEADER,
    eGID_AUDIO,
    eGID_LED,
    eGID_PWM,
    eGID_IR,
    eGID_GPIO,
    eGID_FW,
    eGID_END,
};

//------------------------------------------------------------------------------
// Device ID (eGID_HEADER)
//------------------------------------------------------------------------------
enum {
    eID_HEADER_40,
    eID_HEADER_7,
    eID_HEADER_14,
    eID_HEADER_END,
};

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
extern int  device_check    (int gid, int did, char *resp);
extern int  device_setup    (void);

//------------------------------------------------------------------------------
#endif  // __LIB_DEV_TEST_H__
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------