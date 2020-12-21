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

//#include "time_service.h"
//#include "drivers/timeout.h"
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

#define MAX_NTP_SERVER_LENGTH           20
#define ENDPOINT_LENGTH                 128
#define CLIENTID_LENGTH       128
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
    net.u8SsidLen = (uint8_t) strlen((const char *) p_ssid);
    net.enuChannel = M2M_WIFI_CH_ALL;
    
    pwd.pu8Passphrase = p_pass;
    pwd.u8PassphraseLen = strlen((const char *) p_pass);
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
    field8_set(&wifi_status, WIFI_CONFIGURED, 1);
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
        field8_set(&wifi_status, WIFI_AP_CONNECTING, 1);        
    }
    else
    {
       wifiError =  m2m_wifi_default_connect();
    }
    
    if(M2M_SUCCESS != wifiError)
    {
      field8_set(&wifi_status, WIFI_ERROR, 1);
      debug_printError("Connecting: WIFI_ERROR");
      return M2M_ERR_FAIL;
    }
    
    return M2M_SUCCESS;
}

bool WIFI_disconnect_ap_psk(void)
{
    int8_t m2mDisconnectError;
    if (1 == field8_get(&wifi_status, WIFI_AP_CONNECTED))
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

/*
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
*/

static void wifiCallback(uint8_t msgType, const void *pMsg)
{
    switch (msgType) {
        case M2M_WIFI_RESP_CON_STATE_CHANGED:
        {
            tstrM2mWifiStateChanged *pstrWifiState = (tstrM2mWifiStateChanged *)pMsg;
            if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED) 
            {
                debug_printInfo("wifiCallback: STATE IS M2M_WIFI_CONNECTED");

                field8_set(&wifi_status, WIFI_AP_CONNECTED, 1);
                field8_set(&wifi_status, WIFI_AP_CONNECTING, 0);
                field8_set(&wifi_notifications, NOTF_CONN_CHANGED, 1);
                WIFI_invoke_connection_info();
            } 
            else if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED) 
            {
                debug_printInfo("wifiCallback: STATE IS M2M_WIFI_DISCONNECTED");
                field8_set(&wifi_status, WIFI_AP_CONNECTED, 0);
                field8_set(&wifi_status, WIFI_AP_CONNECTING, 0);
                field8_set(&wifi_status, WIFI_SOCKET_CONNECTED, 0);
                field8_set(&wifi_notifications, NOTF_CONN_CHANGED, 1);
            }
            
            // Not understood yet why this existed. Seems unnecessary.
            //if ( ( wifiConnectionStateChangedCallback != NULL) &&
            //     ( shared_networking_params.amDisconnecting == 0))
            //{
            //    wifiConnectionStateChangedCallback(pstrWifiState->u8CurrState);
            //}
            break;
        }
        
        case M2M_WIFI_REQ_DHCP_CONF:
        {
            debug_printInfo("Received DHCP response");

            if (gethostbyname((const char *) endpoint) == M2M_SUCCESS)
            {
            }
            field8_set(&wifi_notifications, NOTF_DHCP_FINISHED, 1);
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
                strcpy(net.pu8Ssid, (const char *)pstrProvInfo->au8SSID);
                strcpy(pwd.pu8Passphrase, (const char *)pstrProvInfo->au8Password);
                pwd.u8PassphraseLen = strlen((const char *)pstrProvInfo->au8Password);
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
            field8_set(&wifi_notifications, NOTF_CONN_INFO, 1);
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

bool WIFI_is_ap_connecting(void) {
    return field8_is(&wifi_status, WIFI_AP_CONNECTING, 1);
}

bool WIFI_is_socket_connected(void) {
    return field8_is(&wifi_status, WIFI_SOCKET_CONNECTED, 1);
}

bool WIFI_is_socket_connecting(void) {
    return field8_is(&wifi_status, WIFI_SOCKET_CONNECTING, 1);
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
    if (field8_get(field, key))
        return 1;
    return 0;
}


void WIFI_ExchangeBufferInit(exchangeBuffer *buffer)
{
	buffer->currentLocation = buffer->start;
	buffer->dataLength = 0;
}

uint16_t WIFI_ExchangeBufferWrite(exchangeBuffer *buffer, uint8_t *data, uint16_t length)
{
	uint8_t *bend = buffer->start + buffer->bufferLength - 1;
	uint8_t *dend = (buffer->currentLocation - buffer->start + buffer->dataLength) % buffer->bufferLength + buffer->start;
    uint16_t i = 0;

	for (i = length; i > 0; i--) 
	{
		if (dend > bend)
		{
			dend = buffer->start;
		}
		if (buffer->dataLength != 0 && dend == buffer->currentLocation)
		{
			break;
		}
		*dend = *data;
		dend++;
		data++;
		buffer->dataLength++;
	}
    
	return length; 
}

uint16_t WIFI_ExchangeBufferPeek(exchangeBuffer *buffer, uint8_t *data, uint16_t length)
{
	uint8_t *ptr = buffer->currentLocation;
	uint8_t *bend = buffer->start + buffer->bufferLength - 1;
	uint16_t i = 0;

	for (i = 0; i < length && i < buffer->dataLength; i++) 
    {
		data[i] = ptr[i];
		if (ptr > bend)
        {
			ptr = buffer->start;
        }
	}

	return i;
}

uint16_t MQTT_ExchangeBufferRead(exchangeBuffer *buffer, uint8_t *data, uint16_t length)
{
	uint8_t *bend = buffer->start + buffer->bufferLength - 1;
	uint16_t i = 0;

	for (i = 0; i < length && buffer->dataLength > 0; i++) 
    {
		data[i] = *buffer->currentLocation;
		buffer->currentLocation++;
		buffer->dataLength--;
		if (buffer->currentLocation > bend)
        {
			buffer->currentLocation = buffer->start;
        }
	}
	return i; 
}




#define MAX_SUPPORTED_SOCKETS		2
/**********************BSD (WINC) Enumerator Translators ********************************/
typedef enum
{
	WINC_AF_INET = 2,
}wincSupportedDomains_t;

typedef enum
{
	WINC_STREAM = 1,
	WINC_DGRAM = 2,
}wincSupportedTypes_t;

typedef enum
{
	WINC_NON_TLS = 0,
	WINC_TLS = 0x01,
}wincSupportedProtocol_t;

typedef enum
{
	WINC_SOCK_ERR_NO_ERROR = 0,
	WINC_SOCK_ERR_INVALID_ADDRESS = -1,
	WINC_SOCK_ERR_ADDR_ALREADY_IN_USE = -2,
	WINC_SOCK_ERR_MAX_TCP_SOCK = -3,
	WINC_SOCK_ERR_MAX_UDP_SOCK = -4,
	WINC_SOCK_ERR_INVALID_ARG = -6,
	WINC_SOCK_ERR_MAX_LISTEN_SOCK = -7,
	WINC_SOCK_ERR_INVALID = -9,
	WINC_SOCK_ERR_ADDR_IS_REQUIRED = -11,
	WINC_SOCK_ERR_CONN_ABORTED = -12,
	WINC_SOCK_ERR_TIMEOUT = -13,
	WINC_SOCK_ERR_BUFFER_FULL = -14,
}wincSocketResponses_t;

/*******************WINC specific socket address structure***************************/
typedef struct {
    uint16_t		sa_family;		//Socket address
    uint8_t		sa_data[14];	// Maximum size of all the different socket address structures.
}wincSupported_sockaddr;

/**********************BSD (Private) Global Variables ********************************/
static bsdErrno_t bsdErrorNumber;

static packetReceptionHandler_t *packetRecvInfo;

/**********************BSD (Private) Function Prototypes *****************************/
static void bsd_setErrNo (bsdErrno_t errorNumber);

/**********************BSD (Private) Function Implementations ************************/
static void bsd_setErrNo (bsdErrno_t errorNumber)
{
	bsdErrorNumber = errorNumber;
}

/**********************BSD (Public) Function Implementations **************************/
bsdErrno_t BSD_GetErrNo(void)
{
	return bsdErrorNumber;
}

packetReceptionHandler_t* getSocketInfo(uint8_t sock)
{
   uint8_t i = 0;
   if (sock >= 0)
   {
      packetReceptionHandler_t *bsdSocketInfo = BSD_GetRecvHandlerTable();
      for(i = 0; i < MAX_SUPPORTED_SOCKETS; i++)
      {
         if(bsdSocketInfo)
         {
            if(*(bsdSocketInfo->socket) == sock)
            {
               return bsdSocketInfo;
            }
         }
         bsdSocketInfo++;
      }
   }
   return NULL;
}

int BSD_socket(int domain, int type, int protocol)
{
    wincSupportedDomains_t wincDomain;
    wincSupportedTypes_t wincType;
    wincSupportedProtocol_t wincProtocol;
    int8_t wincSocketReturn;

	switch ((bsdDomain_t)domain)
	{
		case PF_INET:
			wincDomain = WINC_AF_INET;
		break;
		default:	// Domain Not Implemented by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	switch ((bsdTypes_t)type)
	{
		case BSD_SOCK_STREAM:
			wincType = WINC_STREAM;
		break;
		case BSD_SOCK_DGRAM:
			wincType = WINC_DGRAM;
		break;
		default:	// Type Not Implemented by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	switch ((wincSupportedProtocol_t)protocol)
	{
		case WINC_NON_TLS:
		case WINC_TLS:
			wincProtocol = protocol;
		break;
		default:	// Protocol Not Implemented by WINC
			bsd_setErrNo(EINVAL);
			return BSD_ERROR;
	}

   wincSocketReturn = socket((uint16_t)wincDomain, (uint8_t)wincType, (uint8_t)wincProtocol);
   if (wincSocketReturn < 0)			   // WINC Socket Access Denied always returns -1 for failure to get socket
	{
		bsd_setErrNo(EACCES);
		return BSD_ERROR;
	}

   return wincSocketReturn;		// >= 0 represents SUCCESS
}

int BSD_connect(int socket, const struct bsd_sockaddr *name, socklen_t namelen)
{
   int returnValue = BSD_ERROR;
	wincSocketResponses_t wincConnectReturn = WINC_SOCK_ERR_INVALID;
	wincSupported_sockaddr winc_sockaddr;

    packetReceptionHandler_t *bsdSocket = getSocketInfo(socket);
    if(!bsdSocket)
    {

    }
    else
    {
	    winc_sockaddr.sa_family = name->sa_family;
	    memcpy(winc_sockaddr.sa_data, name->sa_data, sizeof(winc_sockaddr.sa_data));

      if(winc_sockaddr.sa_family == PF_INET)
      {
            winc_sockaddr.sa_family = AF_INET;
            wincConnectReturn = connect(socket, (struct sockaddr*)&winc_sockaddr, (uint8_t)namelen);
            if(wincConnectReturn != WINC_SOCK_ERR_NO_ERROR)
            {

               switch(wincConnectReturn)
               {
                  case WINC_SOCK_ERR_INVALID_ARG:
                     if(socket < 0)
                     {
                        bsd_setErrNo(ENOTSOCK);
                     }
                     else if(name == NULL)
                     {
                        bsd_setErrNo(EADDRNOTAVAIL);
                     }
                     else if(namelen == 0)
                     {
                        bsd_setErrNo(EINVAL);
                     } else
                     {
                        bsd_setErrNo(ENOTSOCK);
                     }
                     break;
                  case WINC_SOCK_ERR_INVALID:
                     bsd_setErrNo(EIO);
                     break;
                  default:
                     break;
               }
            }
            else
            {

               bsdSocket->socketState = SOCKET_IN_PROGRESS;
               returnValue = BSD_SUCCESS;
            }
		}
      else
      {
			    bsd_setErrNo(EAFNOSUPPORT);
      }
    }
    return returnValue;
}

void BSD_SetRecvHandlerTable(packetReceptionHandler_t *appRecvInfo)
{
	packetRecvInfo = appRecvInfo;
}

packetReceptionHandler_t *BSD_GetRecvHandlerTable()
{
	return packetRecvInfo;
}

int BSD_recv(int socket, const void *buf, size_t len, int flags)
{
    wincSocketResponses_t wincRecvReturn;

	if (flags != 0)
	{	// Flag Not Support by WINC implementation
		bsd_setErrNo(EINVAL);
		return BSD_ERROR;
	}

   wincRecvReturn = recv((SOCKET)socket, (void*)buf, (uint16_t)len, (uint32_t)flags);
	if(wincRecvReturn != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincRecvReturn)
		{
			case WINC_SOCK_ERR_INVALID_ARG:
				if(socket < 0)
				{

					bsd_setErrNo(ENOTSOCK);
				}
				else if(buf == NULL)
				{

					bsd_setErrNo(EFAULT);
				}
				else if(len == 0)
				{

					bsd_setErrNo(EMSGSIZE);
				}
				else
				{

					bsd_setErrNo(EINVAL);
				}
			break;
			case WINC_SOCK_ERR_BUFFER_FULL:

				bsd_setErrNo(ENOBUFS);
			break;
            default:

            break;
		}
		return BSD_ERROR;
	}
	else
	{
		// The socket.c send() API only returns (0) to indicate No Error
		// Current WINC implementation doesn't use returned value per BSD.
		// debug_printGOOD("BSD: Recv Success");

		// TODO: Number of Bytes received should be returned in correct implementation.
		return BSD_SUCCESS;
	}
}

int BSD_close(int socket)
{
   wincSocketResponses_t wincCloseReturn;

   packetReceptionHandler_t* sock = getSocketInfo(socket);
   if (sock != NULL)
   {
      sock->socketState = NOT_A_SOCKET;
   }

   wincCloseReturn = close((SOCKET)socket);

	if (wincCloseReturn!= WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincCloseReturn)
		{
		   case WINC_SOCK_ERR_INVALID_ARG:
		       bsd_setErrNo(EBADF);
		   break;
		   case WINC_SOCK_ERR_INVALID:
		       bsd_setErrNo(EIO);
		   break;
		   default:
		   break;
		}
		return BSD_ERROR;
	}
	else
	{
        return BSD_SUCCESS;
	}
}

uint32_t BSD_htonl(uint32_t hostlong)
{
	return _htonl(hostlong);
}

uint16_t BSD_htons(uint16_t hostshort)
{
	return _htons(hostshort);
}

uint32_t BSD_ntohl(uint32_t netlong)
{
	return _ntohl(netlong);
}

uint16_t BSD_ntohs(uint16_t netshort)
{
	return _ntohs(netshort);
}

int BSD_bind(int socket, const struct bsd_sockaddr *addr, socklen_t addrlen)
{
	wincSocketResponses_t wincBindReturn;
	static wincSupported_sockaddr winc_sockaddr;

	winc_sockaddr.sa_family = addr->sa_family;
	memcpy((void*)winc_sockaddr.sa_data, (const void *)addr->sa_data, sizeof(winc_sockaddr.sa_data));

	switch(winc_sockaddr.sa_family)
	{
		case PF_INET:
			winc_sockaddr.sa_family = AF_INET;
			wincBindReturn = bind((int8_t)socket, (struct sockaddr*)&winc_sockaddr, (uint8_t)addrlen);
		break;
		default:		//Address family not supported by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	if (wincBindReturn != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincBindReturn)
		{
			case WINC_SOCK_ERR_INVALID_ARG:
				if(socket < 0)
				{
					bsd_setErrNo(ENOTSOCK);
				}
				else if(addr != NULL)
				{
					bsd_setErrNo(EFAULT);
				}
				else if(addrlen == 0)
				{
					bsd_setErrNo(EINVAL);
				}
				break;
			case WINC_SOCK_ERR_INVALID:
				bsd_setErrNo(EIO);
				break;
			default:
				break;
		}
		return BSD_ERROR;
	}
	else
	{
		return BSD_SUCCESS;
	}
}

int BSD_recvfrom(int socket, void *buf,	size_t len, int flags, struct bsd_sockaddr *from, socklen_t *fromlen)
{
    wincSocketResponses_t wincRecvFromReturn;
	wincSupported_sockaddr winc_sockaddr;

	if (flags != 0)
	{	// Flag Not Support by WINC implementation
		bsd_setErrNo(EINVAL);
		return BSD_ERROR;
	}

	winc_sockaddr.sa_family = from->sa_family;
	memcpy((void*)winc_sockaddr.sa_data, (const void *)from->sa_data, sizeof(winc_sockaddr.sa_data));

	switch(winc_sockaddr.sa_family)
	{
		case PF_INET:
			winc_sockaddr.sa_family = AF_INET;
			wincRecvFromReturn = recvfrom((SOCKET)socket, buf, (uint16_t)len, (uint32_t)flags);
		break;
		default:		//Address family not supported by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	if(wincRecvFromReturn != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincRecvFromReturn)
		{
			case WINC_SOCK_ERR_INVALID_ARG:
				if(socket < 0)
				{
					bsd_setErrNo(ENOTSOCK);
				}
				else if(buf == NULL)
				{
					bsd_setErrNo(EFAULT);
				}
				else if(len == 0)
				{
					bsd_setErrNo(EMSGSIZE);
				}
				else
				{
					bsd_setErrNo(EINVAL);
				}
			break;
			case WINC_SOCK_ERR_BUFFER_FULL:
				bsd_setErrNo(ENOBUFS);
			break;
            default:
            break;
		}
		return BSD_ERROR;
	}
	else
	{
		// The socket.c send() API only returns (0) to indicate No Error
		// Current WINC implementation doesn't use returned value per BSD.

		// TODO: Number of Bytes received should be returned in correct implementation.
		return BSD_SUCCESS;
	}
}

int BSD_listen(int socket, int backlog)
{
	wincSocketResponses_t wincListenResponse;

	wincListenResponse = listen ((SOCKET)socket, (uint8_t)backlog);

	if (wincListenResponse != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincListenResponse)
		{
			case SOCK_ERR_INVALID_ARG:
			if (socket < 0)
			{
				bsd_setErrNo(ENOTSOCK);
			}
			else
			{
				bsd_setErrNo(EINVAL);
			}
			break;
			case SOCK_ERR_INVALID:
				bsd_setErrNo(EIO);
			break;
			default:
			break;
		}
		return BSD_ERROR;
	}
	else
	{
		return BSD_SUCCESS;
	}
}

int WIFI_accept(int socket, struct bsd_sockaddr * addr, socklen_t * addrlen)
{
	wincSocketResponses_t wincAcceptReturn;
	wincSupported_sockaddr winc_sockaddr;

	winc_sockaddr.sa_family = addr->sa_family;
	memcpy((void*)winc_sockaddr.sa_data, (const void *)addr->sa_data, sizeof(winc_sockaddr.sa_data));

	switch(winc_sockaddr.sa_family)
	{
		case PF_INET:
			winc_sockaddr.sa_family = AF_INET;
			wincAcceptReturn = accept((SOCKET)socket, (struct sockaddr*)&winc_sockaddr, (uint8_t *)addrlen);
		break;
		default:		//Address family not supported by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	if (wincAcceptReturn != WINC_SOCK_ERR_NO_ERROR)
	{
		if (socket < 0)
		{
			bsd_setErrNo(ENOTSOCK);
		}
		else
		{
			bsd_setErrNo(EINVAL);
		}
		return M2M_ERR_FAIL;
	}
	else
	{
		return M2M_SUCCESS;
    }
}

int BSD_getsockopt(int socket, int level, int optname, void * optval, socklen_t * optlen)
{
	bsd_setErrNo(ENOSYS);
	return BSD_ERROR;
}

int BSD_setsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen)
{
	wincSocketResponses_t wincSockOptResponse;
	wincSupportedSockLevel wincSockLevel;
	wincSupportedSockOptions wincSockOptions;

	switch((wincSupportedSockLevel)level)
	{
		case BSD_SOL_SOCKET:
		case BSD_SOL_SSL_SOCKET:
			wincSockLevel = level;
		break;
		default:
			bsd_setErrNo(EIO);
		return BSD_ERROR;

	}
	switch((wincSupportedSockOptions)optname)
	{
		case BSD_SO_SSL_BYPASS_X509_VERIF:
		case BSD_SO_SSL_SNI:
		case BSD_SO_SSL_ENABLE_SESSION_CACHING:
		case BSD_SO_SSL_ENABLE_SNI_VALIDATION:
			wincSockOptions = optname;
		break;
		default:
			bsd_setErrNo(EIO);
		return BSD_ERROR;
	}
    wincSockOptResponse = setsockopt((SOCKET)socket, (uint8_t) wincSockLevel, (uint8_t) wincSockOptions, optval, (uint16_t)optlen);
	if (wincSockOptResponse != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincSockOptResponse)
		{
			case SOCK_ERR_INVALID_ARG:
				if (socket < 0)
				{
					bsd_setErrNo(ENOTSOCK);
				}
				else if (optval == NULL)
				{
					bsd_setErrNo(EFAULT);
				}
				else
				{
					bsd_setErrNo(EINVAL);
				}
				break;
			case SOCK_ERR_INVALID:
				bsd_setErrNo(EIO);
				break;
			default:
				break;
		}
		return BSD_ERROR;
	}
	else
	{
		return BSD_SUCCESS;
	}
}

int BSD_write(int fd, const void *buf, size_t nbytes)
{
	bsd_setErrNo(ENOSYS);
	return BSD_ERROR;
}

int BSD_read(int fd, void *buf, size_t nbytes)
{
	bsd_setErrNo(ENOSYS);
	return BSD_ERROR;
}

int BSD_poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	bsd_setErrNo(ENOSYS);
	return BSD_ERROR;
}

socketState_t BSD_GetSocketState(int sock)
{
	socketState_t sockState;
	packetReceptionHandler_t *bsdSocketInfo;

	sockState = NOT_A_SOCKET;
	bsdSocketInfo = getSocketInfo(sock);

	if(bsdSocketInfo)
	{
	   sockState = bsdSocketInfo->socketState;
	}

	return sockState;
}

int BSD_send(int socket, const void *msg, size_t len, int flags)
{
   wincSocketResponses_t wincSendReturn;

   if (flags != 0)
   {	// Flag Not Support by WINC implementation
      bsd_setErrNo(EINVAL);
      return BSD_ERROR;
   }

   wincSendReturn = send((SOCKET)socket, (void*)msg, (uint16_t)len, (uint16_t)flags);
   if(wincSendReturn != WINC_SOCK_ERR_NO_ERROR)
   {

      // Most likely in this case we HAVE to update the socket state, especially if we get ENOTSOCK !!!
      switch(wincSendReturn)
      {
         case WINC_SOCK_ERR_INVALID_ARG:
         if(socket < 0)
         {
            bsd_setErrNo(ENOTSOCK);
         }
         else if(msg == NULL)
         {
            bsd_setErrNo(EFAULT);
         }
         else if(len > SOCKET_BUFFER_MAX_LENGTH)
         {
            bsd_setErrNo(EMSGSIZE);
         }
         else
         {
            bsd_setErrNo(EINVAL);
         }
         break;
         case WINC_SOCK_ERR_BUFFER_FULL:
         bsd_setErrNo(ENOBUFS);
         break;
         default:
         break;
      }
      return BSD_ERROR;
   }
   else
   {
      // The socket.c send() API either sends the entire packet or
      // does not send the packet at all. Therefore, if it does
      // successfully send the packet, 'len' number of bytes will
      // be transmitted. In this case, it is safe to return the
      // value of 'len' as the number of bytes sent.
      return len;
   }
}

int BSD_sendto(int socket, const void *msg, size_t len,	int flags, const struct bsd_sockaddr *to, socklen_t tolen)
{
	wincSocketResponses_t wincSendToResponse;
	wincSupported_sockaddr winc_sockaddr;

	if (flags != 0)
	{	// Flag Not Support by WINC implementation
		bsd_setErrNo(EINVAL);
		return BSD_ERROR;
	}

	winc_sockaddr.sa_family = to->sa_family;
	memcpy((void*)winc_sockaddr.sa_data, (const void *)to->sa_data, sizeof(winc_sockaddr.sa_data));

	switch(winc_sockaddr.sa_family)
	{
		case PF_INET:
			winc_sockaddr.sa_family = AF_INET;

			wincSendToResponse = sendto((SOCKET)socket, (void *)msg, (uint16_t)(len), (uint16_t)flags, (struct sockaddr *)&winc_sockaddr, (uint8_t)tolen);
		break;
		default:		//Address family not supported by WINC
			bsd_setErrNo(EAFNOSUPPORT);
			return BSD_ERROR;
	}

	if (wincSendToResponse != WINC_SOCK_ERR_NO_ERROR)
	{
		switch(wincSendToResponse)
		{
			case SOCK_ERR_INVALID_ARG:
				if(socket < 0)
				{
					bsd_setErrNo(ENOTSOCK);
				}
				else if(msg == NULL)
				{
					bsd_setErrNo(EFAULT);
				}
				else if(len > SOCKET_BUFFER_MAX_LENGTH)
				{
					bsd_setErrNo(EMSGSIZE);
				}
				else
				{
					bsd_setErrNo(EINVAL);
				}
			break;
			case SOCK_ERR_BUFFER_FULL:
				bsd_setErrNo(ENOBUFS);
			break;
			default:
			break;
		}
		return BSD_ERROR;
	}
	else
	{
		// The socket.c send() API either sends the entire packet or
		// does not send the packet at all. Therefore, if it does
		// successfully send the packet, 'len' number of bytes will
		// be transmitted. In this case, it is safe to return the
		// value of 'len' as the number of bytes sent.
		return len;
	}
}

void WIFI_socket_callback(int8_t sock, uint8_t msgType, void *pMsg)
{
	packetReceptionHandler_t *bsdSocketInfo;

	bsdSocketInfo = getSocketInfo(sock);
   if(bsdSocketInfo == NULL) {

      return;
   }
	switch (msgType)
	{
		case SOCKET_MSG_CONNECT:
         if(pMsg)
         {
            	tstrSocketConnectMsg *pstrConnect = (tstrSocketConnectMsg *)pMsg;
            if (pstrConnect->s8Error >= 0)
            {

               bsdSocketInfo->socketState = SOCKET_CONNECTED;
            }
            else
            {

               BSD_close(sock);
            }
         }
		break;

		case SOCKET_MSG_SEND:
		   bsdSocketInfo->socketState = SOCKET_CONNECTED;
		break;

      case SOCKET_MSG_RECV:
         if(pMsg)
         {
          	tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pMsg;
            if (pstrRecv->s16BufferSize > 0)
            {
	            bsdSocketInfo->recvCallBack(pstrRecv->pu8Buffer, pstrRecv->s16BufferSize);
	            bsdSocketInfo->socketState = SOCKET_CONNECTED;

            } else {

               BSD_close(sock);
            }
         }
      break;

		case SOCKET_MSG_RECVFROM:
		{
         if(pMsg)
         {
			   tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pMsg;
			   if (pstrRecv->pu8Buffer && pstrRecv->s16BufferSize)
			   {
				   bsdSocketInfo->recvCallBack(pstrRecv->pu8Buffer, pstrRecv->s16BufferSize);
				   bsdSocketInfo->socketState = SOCKET_CONNECTED;
			   }  else {
			      BSD_close(sock);
            }
         }
		}
		break;

		default:

		break;
	}

}
