/*
\file   wifi_service.c

\brief  Wifi service source file.

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

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "clock.h"
#include <libpic30.h>

#include "wifi_service.h"

#include "time_service.h"
#include "drivers/timeout.h"
#include "debug_print.h"
#include "conf_winc.h"

#include "winc/m2m/m2m_wifi.h"
#include "winc/common/winc_defines.h"
#include "winc/driver/winc_adapter.h"
#include "winc/common/winc_defines.h"
#include "winc/m2m/m2m_types.h"
#include "winc/m2m/m2m_wifi.h"
#include "winc/socket/socket.h"
#include "winc/spi_flash/spi_flash.h"
#include "winc/spi_flash/spi_flash_map.h"

#include "IoT_Sensor_Node_config.h"

uint8_t wifi_status = 0;
uint8_t wifi_notifications = 0;
void    field8_set(uint8_t *field, uint8_t key, uint8_t value);
uint8_t field8_get(uint8_t *field, uint8_t key);
uint8_t field8_is(uint8_t *field, uint8_t key, uint8_t value);

//Flash location to read thing Name from winc
#define THING_NAME_FLASH_OFFSET (M2M_TLS_SERVER_FLASH_OFFSET + M2M_TLS_SERVER_FLASH_SIZE - FLASH_PAGE_SZ) 
#define AWS_ENDPOINT_FLASH_OFFSET (THING_NAME_FLASH_OFFSET - FLASH_PAGE_SZ)
#define CLOUD_WIFI_TASK_INTERVAL        50L
#define CLOUD_NTP_TASK_INTERVAL         1000L
#define SOFT_AP_CONNECT_RETRY_INTERVAL  1000L

#define MAX_NTP_SERVER_LENGTH           20


timerStruct_t ntpTimeFetchTimer  = {ntpTimeFetchTask};
	
uint32_t checkBackTask(void * param);
timerStruct_t checkBackTimer  = {checkBackTask};	

shared_networking_params_t shared_networking_params;

#define ENDPOINT_LENGTH 128
#define CLIENTID_LENGTH 128
static uint8_t* endpoint;
static uint8_t* client_id;

tstrNetworkId net;
tstrAuthPsk pwd = {NULL, NULL, 0};

static bool responseFromProvisionConnect = false;

void (*callback_funcPtr)(uint8_t);

// Callback function pointer for indicating status updates upwards
void  (*wifiConnectionStateChangedCallback)(uint8_t  status) = NULL;

// Function to be called by WifiModule on status updates from below
static void wifiCallback(uint8_t msgType, const void *pMsg);

// This is a workaround to wifi_deinit being broken in the winc, so we can de-init without hanging up
int8_t winc_hif_deinit(void * arg);

static tstrM2MConnInfo connectionInfo;

void WIFI_commission_ap_psk(uint8_t* p_ssid, uint8_t* p_pass) {
    net.pu8Bssid = NULL;
    net.pu8Ssid = p_ssid;
    net.u8SsidLen = (uint8_t) strlen(p_ssid);
    net.enuChannel = M2M_WIFI_CH_ALL;
    
    pwd.pu8Passphrase = p_pass;
    pwd.u8PassphraseLen = strlen(p_pass);
}

void WIFI_provision_endpoint(uint8_t* host, uint8_t port) {

}

void WIFI_invoke_handle_events(void) {
    m2m_wifi_handle_events(NULL);
}

void WIFI_invoke_connection_info(void) {
    m2m_wifi_get_connection_info();
}

uint8_t* WIFI_get_ip_address_value() {
    //static uint8_t ip_address[4];
    
    //= connectionInfo.au8IPAddr;
    return connectionInfo.au8IPAddr;
}

/*
 * This function needs some work
 */
tstrWifiInitParam param ;


void WIFI_init(void (*funcPtr)(uint8_t), uint8_t mode)
{
    callback_funcPtr = funcPtr;

    /* Initialize Wi-Fi parameters structure. */
    memset((uint8_t *)&param, 0, sizeof(tstrWifiInitParam));

    /* Initialize Connection information structure */
    memset(&connectionInfo, 0, sizeof(tstrM2MConnInfo));

    /* set the callback to run on connection,*/
    param.pfAppWifiCb = wifiCallback;

    /* clear the socket just in case there's dirty buffer*/
    socketDeinit();
    
    /* reset the WINC hardware interface */
    winc_hif_deinit(NULL);

    winc_adapter_deinit();

    wifiConnectionStateChangedCallback = callback_funcPtr;
    
    m2m_wifi_enable_dhcp(1);
    
    // cannot use M2M_SUCCESS because M2M_SUCCESS is zero and winc_adapter_init
    // returns 1 on success.
    if ( 1 != winc_adapter_init() ) {
        debug_printError("WINC initialization failed - program halted.");
        while(1);
    }

    if ( M2M_SUCCESS != m2m_wifi_init(&param)) {
        debug_printError("WINC initialization failed - program halted.");
        while(1);        
    }
    
    m2m_wifi_request_scan(M2M_WIFI_CH_ALL);
    
    socketInit();
}

void wifi_readThingNameFromWinc()
{
    int8_t status;
    status =  m2m_wifi_download_mode();
    
    if(status != M2M_SUCCESS)
    {
        debug_printError("WINC download mode failed - Thing Name cannot be obtained");
    }
    else
    {
        debug_printInfo("WINC in download mode");
        		
	    status = spi_flash_read(client_id, THING_NAME_FLASH_OFFSET, CLIENTID_LENGTH);        
        if(status != M2M_SUCCESS || client_id[0] == 0xFF || client_id[CLIENTID_LENGTH - 1] == 0xFF)
        {
            sprintf((char *) client_id, "%s", DEFAULT_HOSTNAME); 
            debug_printIoTAppMsg("Thing Name is not present, error type %d, user defined thing ID is used",status);
        }
        else 
        {
            debug_printIoTAppMsg("Thing Name read from the device is %s",client_id);
        }
    }
}

void wifi_readEndpointFromWinc()
{
    int8_t status;
    
    status =  m2m_wifi_download_mode();
    
    if(status != M2M_SUCCESS)
    {
        debug_printError("WINC download mode failed - AWS Host URL cannot be obtained");
    }
    else
    {        
        debug_printInfo("WINC in download mode");

        status = spi_flash_read((uint8_t*)endpoint, AWS_ENDPOINT_FLASH_OFFSET, AWS_ENDPOINT_LEN);
        if(status != M2M_SUCCESS )
        {
            debug_printError("Error reading AWS Endpoint from WINC");
        }
        else if(endpoint[0] == 0xFF)
        {
            debug_printIoTAppMsg("AWS Endpoint is not present in WINC, either re-provision or microchip AWS sandbox endpoint will be used");
        }
        else
        {
            debug_printIoTAppMsg("AWS Endpoint read from WINC is %s",endpoint);  
        }
    }
}

bool WIFI_connect_ap_psk(uint8_t passed_wifi_creds)
{
    int8_t wifiError = 0;

    
    if(passed_wifi_creds == NEW_CREDENTIALS)
    {
        debug_printInfo("Connecting: SSID: [%s] PASS: [%s]", net.pu8Ssid, pwd.pu8Passphrase);
        wifiError = m2m_wifi_connect_psk(WIFI_CRED_SAVE_UNENCRYPTED, &net, &pwd);
    }
    else
    {
       wifiError =  m2m_wifi_default_connect();
    }
    
    if(M2M_SUCCESS != wifiError)
    {
      debug_printError("WIFI: wifi error = %d", wifiError);
      shared_networking_params.haveError = 1;
      return M2M_ERR_FAIL;
    }
    else {
      debug_printInfo("WIFI: connect successful");
    }
    
    //while(1){
         /* Handle the app state machine plus the WINC event handler */
    //    while(m2m_wifi_handle_events(NULL) != M2M_SUCCESS) {
    //}
    return M2M_SUCCESS;
}

bool WIFI_disconnect_ap_psk(void)
{
    int8_t m2mDisconnectError;
    if(shared_networking_params.haveAPConnection == 1)
    {
        if(M2M_SUCCESS != (m2mDisconnectError=m2m_wifi_disconnect()))
        {
            debug_printError("WIFI: Disconnect from AP error = %d",m2mDisconnectError);
            return false;
        }	   
    }
    return true;
}

// Update the system time every CLOUD_NTP_TASK_INTERVAL milliseconds
// Once time is obtained from NTP server WINC maintains the time internally. 
// The WINC will re-sync the time with NTP server utmost once per day or on DHCP renewal
uint32_t ntpTimeFetchTask(void *payload)
{
    if((strncmp(ntpServerName, CFG_NTP_SERVER, strlen(CFG_NTP_SERVER))) != 0)
    {
        strcpy(ntpServerName, CFG_NTP_SERVER);
        debug_printInfo("NTP server name: %s", ntpServerName);
        m2m_wifi_configure_sntp((uint8_t*)ntpServerName, sizeof(ntpServerName), SNTP_ENABLE_DHCP);
    }    
    m2m_wifi_get_system_time();
    
    return CLOUD_NTP_TASK_INTERVAL;
}


uint32_t checkBackTask(void * param)
{
    debug_printError("wifi_cb: M2M_WIFI_RESP_CON_STATE_CHANGED: DISCONNECTED");
    shared_networking_params.haveAPConnection = 0;
    shared_networking_params.amDisconnecting = 0;
    shared_networking_params.amConnectingSocket = 0;
    shared_networking_params.amConnectingAP = 1;
    return 0;
}

static void wifiCallback(uint8_t msgType, const void *pMsg)
{
    switch (msgType) {
        case M2M_WIFI_RESP_CON_STATE_CHANGED:
        {
            tstrM2mWifiStateChanged *pstrWifiState = (tstrM2mWifiStateChanged *)pMsg;
            if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED) 
            {
                field8_set(&wifi_status, WIFI_AP_CONNECTED, 1);
                field8_set(&wifi_notifications, NOTF_CONN_CHANGED, 1);
            } 
            else if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED) 
            {
                field8_set(&wifi_status, WIFI_AP_CONNECTED, 0);
                field8_set(&wifi_status, WIFI_SOCKET_CONNECTED, 0);
                field8_set(&wifi_notifications, NOTF_CONN_CHANGED, 1);
            }
            
            if ( ( wifiConnectionStateChangedCallback != NULL) &&
                 ( shared_networking_params.amDisconnecting == 0))
            {
                wifiConnectionStateChangedCallback(pstrWifiState->u8CurrState);
            }
            break;
        }
        
        case M2M_WIFI_REQ_DHCP_CONF:
        {
            if (gethostbyname((const char *) endpoint) == M2M_SUCCESS)
            {
                field8_set(&wifi_notifications, NOTF_DHCP_FINISHED, 1);
                field8_set(&wifi_status, WIFI_DHCP_COMPLETED, 1);
            }
            break;
        }

        case M2M_WIFI_RESP_GET_SYS_TIME:
        {
            tstrSystemTime* WINCTime = (tstrSystemTime*)pMsg;
            
            // Convert to UNIX_EPOCH, this mktime uses years since 1900 and months are 0 based so we
            //    are doing a couple of adjustments here.
            if(WINCTime->u16Year > 0)
            {
		        TIME_ntpTimeStamp(WINCTime);
            }
            break;
        }
       
        
        case M2M_WIFI_RESP_PROVISION_INFO:
        {
            tstrM2MProvisionInfo *pstrProvInfo = (tstrM2MProvisionInfo*)pMsg;
            if(pstrProvInfo->u8Status == M2M_SUCCESS)
            {
                //sprintf((char*)authType, "%d", pstrProvInfo->u8SecType);
                //debug_printInfo("%s",pstrProvInfo->au8SSID);			   			   
                strcpy(net.pu8Ssid, (uint8_t *)pstrProvInfo->au8SSID);
                strcpy(pwd.pu8Passphrase, (uint8_t*)pstrProvInfo->au8Password);
                pwd.u8PassphraseLen = strlen((uint8_t*)pstrProvInfo->au8Password);
                debug_printInfo("SOFT AP: Connect Credentials sent to WINC");
                responseFromProvisionConnect = true;
            }
            break;
        }
        
        case M2M_WIFI_RESP_CONN_INFO:
        {
            tstrM2MConnInfo *pstrConnInfo = (tstrM2MConnInfo*) pMsg;
            //connectionInfo.__PAD16__ = pstrConnInfo->__PAD16__;
            //connectionInfo.acSSID = pstrConnInfo->acSSID;
            connectionInfo.au8IPAddr[0] = pstrConnInfo->au8IPAddr[0];
            connectionInfo.au8IPAddr[1] = pstrConnInfo->au8IPAddr[1];
            connectionInfo.au8IPAddr[2] = pstrConnInfo->au8IPAddr[2];
            connectionInfo.au8IPAddr[3] = pstrConnInfo->au8IPAddr[3];
            //connectionInfo.s8RSSI = pstrConnInfo->s8RSSI;
            //connectionInfo.u8CurrChannel = pstrConnInfo->u8CurrChannel;
            //connectionInfo.u8SecType = pstrConnInfo->u8SecType;
            break;
        }

        default:
        {
            debug_printInfo("wifiCallback: DEFAULT (break)");
            break;
        }
    }
}


char ntpServerName[MAX_NTP_SERVER_LENGTH];



bool WIFI_is_configured(void) {
    return field8_is(&wifi_status, WIFI_CONFIGURED, 1);
}

bool WIFI_is_ap_connected(void) {
    return field8_is(&wifi_status, WIFI_AP_CONNECTED, 1);
}

bool WIFI_is_socket_connected(void) {
    return field8_is(&wifi_status, WIFI_SOCKET_CONNECTED, 1);
}

bool WIFI_has_notif_conn_info(void) {
    return field8_is(&wifi_notifications, NOTF_CONN_INFO, 1);
}

void WIFI_set_notif_conn_info(bool value) {
    field8_set(&wifi_notifications, NOTF_CONN_INFO, value);
}

void WIFI_del_notif_conn_info(void) {
    WIFI_set_notif_conn_info(0);
}


void field8_set(uint8_t *field, uint8_t key, uint8_t value) {
    if (field8_get(field, key) != value)
        *field = *field ^ ( 1 << key );
}

uint8_t field8_get(uint8_t *field, uint8_t key) {
    return *field & ( 1 << key );
}

uint8_t field8_is(uint8_t *field, uint8_t key, uint8_t value) {
    if (field8_get(field, key) == value)
        return 1;
    return 0;
}

