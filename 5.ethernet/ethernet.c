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
// Client에서 iperf3 서버 모드로 1번만 실행함.
// Server 명령이 iperf3 -c {ip client} -t 1 -P 1 인 경우 strstr "receiver" 검색
// Server 명령이 iperf3 -c {ip client} -t 1 -P 1 -R 인 경우 strstr "sender" 검색
// 결과값 중 Mbits/sec or Gbits/sec를 찾아 속도를 구함.
//------------------------------------------------------------------------------
const char *odroid_mac_prefix = "001E06";
const char *iperf_run_cmd = "iperf3 -t 1 -c";

// stdlib.h
// strtoul (string, endp, base(10, 16,...))

//------------------------------------------------------------------------------
//
// Configuration
//
//------------------------------------------------------------------------------
struct device_ethernet {
    // ethernet link speed
    int speed;
    // ip value ddd of aaa.bbb.ccc.ddd
    int ip_lsb;
    // mac data validate
    char mac_status;
    // mac str (aabbccddeeff)
    char mac_addr[MAC_STR_SIZE];
};

struct device_ethernet DeviceETHERNET;

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
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
    if (ioctl (fd, SIOCGIFADDR, &ifr) < 0) {
        printf ("iface name = eth0, SIOCGIFADDR ioctl Error!!\n");
        close (fd);
        return 0;
    }
    // board(iface) ip
    memset (if_info, 0, sizeof(if_info));
    inet_ntop (AF_INET, ifr.ifr_addr.sa_data+2, if_info, sizeof(struct sockaddr));

    if ((p_str = strtok (if_info, ".")) != NULL) {
        strtok (NULL, "."); strtok (NULL, ".");

        if ((p_str = strtok (NULL, ".")) != NULL)
            return atoi (p_str);
    }
    return 0;
}

//------------------------------------------------------------------------------
#define SERVER_IP_ADDR  "192.168.20.45"     // test charles pc

static int ethernet_iperf (const char *found_str)
{
    FILE *fp;
    char cmd_line[STR_PATH_LENGTH], retry = 10, *pstr;
    int value = 0;

    memset (cmd_line, 0x00, sizeof(cmd_line));
    sprintf(cmd_line, "%s %s", iperf_run_cmd, SERVER_IP_ADDR);

    if ((fp = popen(cmd_line, "r")) != NULL) {
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
        if (ethernet_link_speed() == speed) {
            return 1;
        }
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
        case 'R':
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
static int ethernet_mac_check (char action, char *resp)
{
    char efuse [EFUSE_SIZE_M1S];

    memset (efuse, 0, sizeof (efuse));

    /* R = eth mac read, I = init value, W = eth mac write */
    switch (action) {
        case 'I':   case 'R':   case 'W':
            if ((action == 'W') && !DeviceETHERNET.mac_status) {
                if (mac_server_request (MAC_SERVER_FACTORY, REQ_TYPE_UUID, "m1s", efuse)) {
                    if (efuse_control (efuse, EFUSE_WRITE)) {
                        memset (efuse, 0, sizeof(efuse));
                        if (efuse_control (efuse, EFUSE_READ)) {
                            DeviceETHERNET.mac_status = efuse_valid_check (efuse);
                            if (DeviceETHERNET.mac_status) {
                                memset (DeviceETHERNET.mac_addr, 0, sizeof (MAC_STR_SIZE));
                                efuse_get_mac (efuse, DeviceETHERNET.mac_addr);
                            }
                            else
                                efuse_control (efuse, EFUSE_ERASE);
                        }
                    }
                }
            }
            if (DeviceETHERNET.mac_status) {
                strncpy (resp, &DeviceETHERNET.mac_addr[6], 6);
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
    int value = 0;

    if (ethernet_link_speed() != 1000)
        ethernet_link_setup (1000);

    /* R = sender speed, W = receiver speed, iperf read */
    switch (action) {
        case 'W':
            if ((value = ethernet_iperf ("sender"))) {
                sprintf (resp, "%06d", value);
                return 1;
            }
            break;
        case 'R':
            if ((value = ethernet_iperf ("receiver"))) {
                sprintf (resp, "%06d", value);
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
static int ethernet_link_check (char action, char *resp)
{
    int value = 0;
    /* S = eth 1G setting, C = eth 100M setting, I = init valuue, R = read link speed */
    switch (action) {
        case 'I':   case 'R':
            if (action == 'R')
                DeviceETHERNET.speed = ethernet_link_speed ();

            value = DeviceETHERNET.speed;
            break;
        case 'S':
            DeviceETHERNET.speed = ethernet_link_speed();
            if (DeviceETHERNET.speed != 1000) {
                if (ethernet_link_setup (1000))
                    DeviceETHERNET.speed = 1000;
            }
            value = DeviceETHERNET.speed == 1000 ? 1 : 0;
            break;
        case 'C':
            DeviceETHERNET.speed = ethernet_link_speed();
            if (DeviceETHERNET.speed != 100) {
                if (ethernet_link_setup (100))
                    DeviceETHERNET.speed = 100;
            }
            value = DeviceETHERNET.speed == 100 ? 1 : 0;
            break;
        default :
            break;
    }
    sprintf (resp, "%06d", DeviceETHERNET.speed);
    return value;
}

//------------------------------------------------------------------------------
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
int ethernet_grp_init (void)
{
    char efuse [EFUSE_SIZE_M1S];

    memset (efuse, 0, sizeof (efuse));
    memset (&DeviceETHERNET, 0, sizeof(DeviceETHERNET));

    // get Board lsb ip address value
    DeviceETHERNET.ip_lsb = get_eth0_ip ();

    // mac status & value
    if (efuse_control (efuse, EFUSE_READ)) {
        DeviceETHERNET.mac_status = efuse_valid_check (efuse);
        if (DeviceETHERNET.mac_status)
            efuse_get_mac (efuse, DeviceETHERNET.mac_addr);
    }

    // link speed
    DeviceETHERNET.speed = ethernet_link_speed();
    return 1;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
