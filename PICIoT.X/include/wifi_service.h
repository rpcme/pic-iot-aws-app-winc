/*
\file   wifi_service.h

\brief  Wifi service header file.

(c) 2018 Microchip Technology Inc. and its subsidiaries.

Subject to your compliance with these terms, you may use Microchip software and any
derivatives exclusively with Microchip products. It is your responsibility to comply with third party
license terms applicable to your use of third party software (including open source software) that
may accompany Microchip software.

THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
FOR A PARTICULAR PURPOSE.

IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
SOFTWARE.
*/ 

#ifndef WIFI_SERVICE_H_
#define WIFI_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_WIFI_CRED_LENGTH 31
#define DEFAULT_CREDENTIALS 0
#define NEW_CREDENTIALS     1
#define WIFI_SOFT_AP  0
#define WIFI_DEFAULT  1


/*
 * These are bit settings on a uint8_t field
 * used for communicating the current state of 
 * affairs in the WINC code.
 */

#define WIFI_CONFIGURED       ( 1 << 0 )
#define WIFI_AP_CONNECTED     ( 1 << 1 )
#define WIFI_STA_CONNECTED    ( 1 << 2 )
#define WIFI_SOCKET_CONNECTED ( 1 << 3 )
#define WIFI_DHCP_COMPLETED   ( 1 << 4 )
#define WIFI_INFO_COMPLETED   ( 1 << 5 )
#define WIFI_ERROR            ( 1 << 7 )

/*
 * These are bit settings on a uint8_t field
 * used for notifying a change to state or 
 * available information
 */

#define NOTF_TIME_AVAILABLE   ( 1 << 0 )
#define NOTF_CONN_INFO        ( 1 << 1 )
#define NOTF_PROV_INFO        ( 1 << 2 )
#define NOTF_CONN_CHANGED     ( 1 << 3 )
#define NOTF_ERROR            ( 1 << 7 )

typedef union
{
    uint8_t allBits;
    struct
    {
        unsigned amDisconnecting :1;           
        unsigned haveAPConnection :1;
        unsigned haveError :1;
        unsigned amConnectingAP : 1;
        unsigned amConnectingSocket : 1;
        unsigned amSoftAP: 1;
        unsigned amDefaultCred : 1;
        unsigned haveDataConnection : 1;
    };
} shared_networking_params_t;


// If you pass a callback function in here it will be called when the AP state changes. Pass NULL if you do not want that.

/*
 * Public functions for using the Wi-Fi service.
 */

void WIFI_init(void (*funcPtr)(uint8_t), uint8_t  mode);
void WIFI_commission_ap_psk(uint8_t* p_ssid, uint8_t* p_pass);
bool WIFI_connect_ap_psk(uint8_t passed_wifi_creds);
bool WIFI_disconnect_ap_psk(void);

void wifi_readThingNameFromWinc(void);
void wifi_readAWSEndpointFromWinc(void);

void WIFI_provision_endpoint(uint8_t* host, uint8_t port);
void WIFI_invoke_connection_info(void);
void WIFI_invoke_handle_events(void);
uint8_t* WIFI_get_ip_address_value(void);

bool WIFI_is_configured(void);
bool WIFI_is_ap_connected(void);
bool WIFI_is_socket_connected(void);

bool WIFI_has_notif_conn_info(void);
void WIFI_set_notif_conn_info(bool value);
void WIFI_del_notif_conn_info(void);

// Scheduler
uint32_t ntpTimeFetchTask(void *payload);
uint32_t wifiHandlerTask(void * param);
uint32_t softApConnectTask(void* param);

#include "../winc/m2m/m2m_types.h"

#define MAX_NTP_SERVER_LENGTH	20

extern char authType[2];
extern char ntpServerName[MAX_NTP_SERVER_LENGTH];

void CREDENTIALS_STORAGE_readNTPServerName(char *serverNameBuffer);
void CREDENTIALS_STORAGE_writeNTPServerName(char *serverNameBuffer);

#endif /* WIFI_SERVICE_H_ */