/*
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

#include <string.h>
#include "system.h"
#include "debug_print.h"
#include "conf_winc.h"
#include "wifi_service.h"

static void  winc_notifier(uint8_t status);

int main(void)
{
    SYSTEM_Initialize();
    
    /*
     * Initialize Wi-Fi
     */
    
    /* BUG BUG - this is taken from Microchip code, this should be encapsulated
     *            in the wifi_service.
     *
     *           When we use this with FreeRTOS, these will be passed along as
     *           Task parameters.
    */
    
    debug_printInfo("Initialize and commission Wi-Fi");
    strcpy(ssid, CFG_MAIN_WLAN_SSID);
    strcpy(pass, CFG_MAIN_WLAN_PSK);
    sprintf((char*)authType, "%d", CFG_MAIN_WLAN_AUTH);
    wifi_init(winc_notifier, WIFI_DEFAULT);
     
    /*
     * Forced provision
     */
   debug_printInfo("Initialize and read certificate");
 
    /*
     * Connect
     */
   debug_printInfo("Create socket connection");

    /*
     * Send 10 packets
     */
   uint8_t cntr = 10;
	while (cntr > 0)
	{
       debug_printInfo("Pushing packet %d", cntr);
        cntr--;
	}

    /*
     * Disconnect socket
     */
   debug_printInfo("Tear down socket connection");
    
    /*
     * Disconnect AP
     */
   debug_printInfo("Disconnect from AP");
    
	return 0;
}


static void  winc_notifier(uint8_t status)
{
    // If we have no AP access we want to retry
    if (status != 1)
    {
    } 
}
