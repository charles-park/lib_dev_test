//------------------------------------------------------------------------------
/**
 * @file ethernet.c
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
//------------------------------------------------------------------------------
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <linux/fb.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

//------------------------------------------------------------------------------
#include "../lib_dev_check.h"
#include "lib_mac/lib_mac.h"
#include "lib_efuse/lib_efuse.h"
#include "ethernet.h"

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#define LINK_SPEED_1G       1000
#define LINK_SPEED_100M     100

/* iperf3_odroid 를 설치하여 사용함. socket통신을 통하여 iperf3 실행하도록 함. */
/* iperf3_odroid build 후 iperf3_odroid실행파일은 /usr/bin으로 복사하여 사용함. */
#if defined (__IPERF3_ODROID__)
    #define IPERF3_RUN_CMD      "iperf3_odroid -R -p 8000 -c"
#else
    // #define IPERF3_RUN_CMD      "iperf3 -t 1 -c"
    #define IPERF3_RUN_CMD      "iperf3 -t 1 -R -c"
#endif

//------------------------------------------------------------------------------
//
// Configuration
//
//------------------------------------------------------------------------------
struct device_ethernet {
    // iperf check value
    char iperf_server_ip[STR_PATH_LENGTH +1];
    int iperf_speed;

    // ethernet link speed
    int speed;
    // ip value ddd of aaa.bbb.ccc.ddd
    int ip_lsb;
    // iperf receiver speed
    int iperf_rx_speed;
    // mac data validate
    char mac_status;
    // mac str (aabbccddeeff)
    char mac_str[MAC_STR_SIZE +1];
    // ip str (aaa.bbb.ccc.ddd)
    char ip_str [sizeof(struct sockaddr)+1];
};

#define DEFAULT_IPERF_SPEED     800
#define DEFAULT_IPERF_SERVER    "192.168.20.45"

//------------------------------------------------------------------------------
//
// Configuration
//
//------------------------------------------------------------------------------
struct device_ethernet DeviceETHERNET = {
    DEFAULT_IPERF_SERVER, DEFAULT_IPERF_SPEED, 0, 0, 0, 0, "", ""
};

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static int get_eth0_ip (void)
{
    int fd;
    struct ifreq ifr;
    char if_info[20], *p_str;

    /* this entire function is almost copied from ethtool source code */
    /* Open control socket. */
    if ((fd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf ("%s : Cannot get control socket\n", __func__);
        return 0;
    }

    /*AF_INET - to define IPv4 Address type.*/
    ifr.ifr_addr.sa_family = AF_INET;

    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ -1);
    if (ioctl (fd, SIOCGIFADDR, &ifr) < 0) {
        printf ("iface name = eth0, SIOCGIFADDR ioctl Error!!\n");
        close (fd);
        return 0;
    }
    // board(iface) ip
    memset (if_info, 0, sizeof(if_info));
    inet_ntop (AF_INET, ifr.ifr_addr.sa_data+2, if_info, sizeof(struct sockaddr));

    /* aaa.bbb.ccc.ddd 형태로 저장됨 (16 bytes) */
    memcpy (DeviceETHERNET.ip_str, if_info, strlen(if_info));

    if ((p_str = strtok (if_info, ".")) != NULL) {
        strtok (NULL, "."); strtok (NULL, ".");

        if ((p_str = strtok (NULL, ".")) != NULL)
            return atoi (p_str);
    }
    return 0;
}

//------------------------------------------------------------------------------
// 10 sec wait & retry
#define IPERF3_RETRY_COUNT   10

static int ethernet_iperf (const char *found_str)
{
    FILE *fp;
    char cmd_line[STR_PATH_LENGTH *2 +1], *pstr, retry = IPERF3_RETRY_COUNT;
    int value = 0;

SERVER_BUSY:
    memset (cmd_line, 0x00, sizeof(cmd_line));
    sprintf(cmd_line, "%s %s", IPERF3_RUN_CMD, DeviceETHERNET.iperf_server_ip);

    if (((fp = popen(cmd_line, "r")) != NULL) && retry) {
        while (1) {
            memset (cmd_line, 0, sizeof (cmd_line));
            if (fgets (cmd_line, sizeof (cmd_line), fp) == NULL)
                break;

            if (strstr (cmd_line, found_str) != NULL) {
                if ((pstr = strstr (cmd_line, "MBytes")) != NULL) {
                    while (*pstr != ' ')    pstr++;
                    value = atoi (pstr);
                }
            }
        }
        pclose(fp);
    }
    if (retry-- && !value) {
        printf ("%s : busy. remain retry = %d, value = %d\n", __func__, retry, value);
        sleep (1);
        goto SERVER_BUSY;
    }
    return value;
}

//------------------------------------------------------------------------------
static int ethernet_link_speed (void)
{
    FILE *fp;
    char cmd_line[STR_PATH_LENGTH];

    if (access ("/sys/class/net/eth0/speed", F_OK) != 0)
        return 0;

    memset (cmd_line, 0x00, sizeof(cmd_line));
    if ((fp = fopen ("/sys/class/net/eth0/speed", "r")) != NULL) {
        memset (cmd_line, 0x00, sizeof(cmd_line));
        if (NULL != fgets (cmd_line, sizeof(cmd_line), fp)) {
            fclose (fp);
            return atoi (cmd_line);
        }
        fclose (fp);
    }
    return 0;
}

//------------------------------------------------------------------------------
static int ethernet_link_setup (int speed)
{
    FILE *fp;
    char cmd_line[STR_PATH_LENGTH], retry = 10;

    memset (cmd_line, 0x00, sizeof(cmd_line));
    sprintf(cmd_line,"ethtool -s eth0 speed %d duplex full", speed);
    if ((fp = popen(cmd_line, "w")) != NULL)
        pclose(fp);

    // timeout 10 sec
    while (retry--) {
        if (ethernet_link_speed() == speed)
            return 1;
        sleep (1);
    }
    return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static int ethernet_ip_check (char action, char *resp)
{
    int value = 0;

    /* R = ip read, I = init value */
    switch (action) {
        case 'R':   case 'W':
            value = get_eth0_ip ();
            break;
        case 'I':
            value = DeviceETHERNET.ip_lsb;
            break;
        default :
            break;
    }
    sprintf (resp, "%06d", value);
    return value ? 1 : 0;
}

//------------------------------------------------------------------------------
static int ethernet_mac_write (const char *model)
{
    char efuse [EFUSE_UUID_SIZE];

    memset (efuse, 0, sizeof (efuse));

    if (mac_server_request (MAC_SERVER_FACTORY, REQ_TYPE_UUID, model, efuse)) {
        if (efuse_control (efuse, EFUSE_WRITE)) {
            memset (efuse, 0, sizeof(efuse));
            if (efuse_control (efuse, EFUSE_READ)) {
                if (efuse_valid_check (efuse)) {
                    memset (DeviceETHERNET.mac_str, 0, MAC_STR_SIZE);
                    efuse_get_mac (efuse, DeviceETHERNET.mac_str);
                    return 1;
                }
            }
        }
    }
    efuse_control (efuse, EFUSE_ERASE);
    return 0;
}
//------------------------------------------------------------------------------
static int ethernet_mac_check (char action, char *resp)
{
    /* R = eth mac read, I = init value, W = eth mac write */
    switch (action) {
        case 'I':   case 'R':   case 'W':
            if ((action == 'W') && !DeviceETHERNET.mac_status)
                DeviceETHERNET.mac_status = ethernet_mac_write ("m1s");

            /* 001E06aabbcc 형태로 저장이며, 앞의 6바이트는 고정이므로 하위 6바이트만 전송함. */
            if (DeviceETHERNET.mac_status) {
                strncpy (resp, &DeviceETHERNET.mac_str[6], 6);
                return 1;
            }
            break;
        default :
            break;
    }
    sprintf (resp, "%06d", 0);
    return 0;
}

//------------------------------------------------------------------------------
static int ethernet_iperf_check (char action, char *resp)
{
    int value = 0, status = 0;

    /* ethernet not link */
    if (!DeviceETHERNET.ip_lsb)
        goto error;

    if (ethernet_link_speed() != LINK_SPEED_1G)
        ethernet_link_setup (LINK_SPEED_1G);

    /* R = sender speed, W = receiver speed, iperf read */
    switch (action) {
        case 'I':
            value  = DeviceETHERNET.iperf_rx_speed;
            status = (DeviceETHERNET.iperf_rx_speed < DeviceETHERNET.iperf_speed) ? 0 : 1;
            break;
        case 'R':   case 'W':
            if (get_eth0_ip ())
                value  = (action == 'R') ? ethernet_iperf ("receiver") : ethernet_iperf ("sender");
            status = (value < DeviceETHERNET.iperf_speed) ? 0 : 1;
            break;
        default :
            break;
    }
error:
    sprintf (resp, "%06d", value);
    return status;
}

//------------------------------------------------------------------------------
static int ethernet_link_check (char action, char *resp)
{
    int status = 0;
    /* S = eth 1G setting, C = eth 100M setting, I = init valuue, R = read link speed */
    switch (action) {
        case 'I':   case 'R':
            if (action == 'R')
                DeviceETHERNET.speed = ethernet_link_speed ();

            status = DeviceETHERNET.speed ? 1 : 0;
            break;
        case 'S':
            DeviceETHERNET.speed = ethernet_link_speed();
            if (DeviceETHERNET.speed != LINK_SPEED_1G) {
                DeviceETHERNET.speed  =
                    ethernet_link_setup (LINK_SPEED_1G);
            }
            status = (DeviceETHERNET.speed == LINK_SPEED_1G) ? 1 : 0;
            break;
        case 'C':
            DeviceETHERNET.speed = ethernet_link_speed();
            if (DeviceETHERNET.speed != LINK_SPEED_100M) {
                DeviceETHERNET.speed  =
                    ethernet_link_setup (LINK_SPEED_100M);
            }
            status = (DeviceETHERNET.speed == LINK_SPEED_100M) ? 1 : 0;
            break;
        default :
            break;
    }
    sprintf (resp, "%06d", DeviceETHERNET.speed);
    return status;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// ip_str은 16바이트 할당되어야 함.
//------------------------------------------------------------------------------
void ethernet_ip_str (char *ip_str)
{
    if (DeviceETHERNET.ip_lsb)
        memcpy (ip_str, DeviceETHERNET.ip_str, strlen (DeviceETHERNET.ip_str));
    else
        sprintf (ip_str, "%03d.%03d.%03d.%03d", 0, 0, 0, 0);
}

//------------------------------------------------------------------------------
// mac_str은 20바이트 할당되어야 함.
//------------------------------------------------------------------------------
void ethernet_mac_str (char *mac_str)
{
    if (DeviceETHERNET.mac_status)
        memcpy (mac_str, DeviceETHERNET.mac_str, strlen (DeviceETHERNET.mac_str));
    else
        sprintf (mac_str, "%012d", 0);
}

//------------------------------------------------------------------------------
int ethernet_check (int id, char action, char *resp)
{
    switch (id) {
        case eETHERNET_IP:      return ethernet_ip_check    (action, resp);
        case eETHERNET_MAC:     return ethernet_mac_check   (action, resp);
        case eETHERNET_IPERF:   return ethernet_iperf_check (action, resp);
        case eETHERNET_LINK:    return ethernet_link_check  (action, resp);
        default:
            break;
    }
    sprintf (resp, "%06d", 0);
    return 0;
}

//------------------------------------------------------------------------------
static void default_config_write (const char *fname)
{
    FILE *fp;
    char value [STR_PATH_LENGTH *2 +1];

    if ((fp = fopen(fname, "wt")) == NULL)
        return;

    // default value write
    fputs   ("# info : iperf server ip, iperf speed \n", fp);
    memset  (value, 0, sizeof(value));
    sprintf (value, "%s,%d,\n", DeviceETHERNET.iperf_server_ip, DeviceETHERNET.iperf_speed);
    fputs   (value, fp);
    fclose  (fp);
}

//------------------------------------------------------------------------------
static void default_config_read (void)
{
    FILE *fp;
    char fname [STR_PATH_LENGTH +1], value [STR_PATH_LENGTH +1], *ptr;

    memset  (fname, 0, STR_PATH_LENGTH);
    sprintf (fname, "%sjig-%s.cfg", CONFIG_FILE_PATH, "ethernet");

    if (access (fname, R_OK) != 0) {
        default_config_write (fname);
        return;
    }

    if ((fp = fopen(fname, "r")) == NULL)
        return;

    while(1) {
        memset (value , 0, STR_PATH_LENGTH);
        if (fgets (value, sizeof (value), fp) == NULL)
            break;

        switch (value[0]) {
            case '#':   case '\n':
                break;
            default :
                // default value write
                // fputs   ("# info : iperf server ip, iperf speed \n", fp);
                if ((ptr = strtok ( value, ",")) != NULL) {
                    memset (DeviceETHERNET.iperf_server_ip, 0, STR_PATH_LENGTH);
                    strcpy (DeviceETHERNET.iperf_server_ip, ptr);
                }
                if ((ptr = strtok ( NULL, ",")) != NULL)
                    DeviceETHERNET.iperf_speed = atoi (ptr);
                break;
        }
    }
    fclose(fp);
}

//------------------------------------------------------------------------------
int ethernet_grp_init (void)
{
    char efuse [EFUSE_UUID_SIZE];

    memset (efuse, 0, sizeof (efuse));

    default_config_read ();

    // get Board lsb ip address int value
    DeviceETHERNET.ip_lsb = get_eth0_ip ();

    if (DeviceETHERNET.ip_lsb) {
        // link speed
        DeviceETHERNET.speed = ethernet_link_speed();

        // iperf speed
        DeviceETHERNET.iperf_rx_speed = ethernet_iperf ("receiver");
    }
    // mac status & value
    if (efuse_control (efuse, EFUSE_READ)) {
        DeviceETHERNET.mac_status = efuse_valid_check (efuse);
        if (!DeviceETHERNET.mac_status && DeviceETHERNET.ip_lsb) {
            if (ethernet_mac_write ("m1s")) {
                memset (efuse, 0, sizeof (efuse));
                efuse_control (efuse, EFUSE_READ);
                DeviceETHERNET.mac_status = efuse_valid_check (efuse);
            }
            else
                printf ("%s : ethernet mac write error! (m1s)\n", __func__);
        }

        if (DeviceETHERNET.mac_status)
            efuse_get_mac (efuse, DeviceETHERNET.mac_str);
    }
    return 1;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
