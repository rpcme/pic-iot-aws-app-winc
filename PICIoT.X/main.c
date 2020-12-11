#include <string.h>
#include "system.h"
#include "uart1.h"
#include "debug_print.h"
#include "conf_winc.h"
#include "wifi_service.h"
#include "crypto_client.h"
#include "CryptoAuth_init.h"
//#include "timeout.h"
//#include "interrupt_manager.h"
#include "delay.h"

static void winc_notifier(uint8_t status);

int main(void) {
    debug_init("pic-iot");
    SYSTEM_Initialize(); // bsp
    timeout_flushAll();
    CRYPTO_CLIENT_initialize(); // crypto_client, will be moved when we later have a submodule

    /*
     * Forced provision
     * 
     * The certificate for the TLS connection comes from the ECC608A, and some
     * helper code has been written for this in crypto_client.
     * 
     */
    //    CRYPTO_CLIENT_print_serial();
    //    CRYPTO_CLIENT_print_certificate();

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
    /*
     * When wrapping this into a Task, these would become the task parameters,
     * and the Task's state machine entry point would initialize these values
     * using these functions.
     */
    WIFI_commission_ap_psk((uint8_t*) CFG_MAIN_WLAN_SSID,
            (uint8_t*) CFG_MAIN_WLAN_PSK);
    WIFI_provision_endpoint((uint8_t*) CFG_MAIN_ENDPOINT,
            (uint8_t) CFG_MAIN_PORT);

    WIFI_init(winc_notifier, WIFI_DEFAULT);
    WIFI_connect_ap_psk((uint8_t) NEW_CREDENTIALS);

    while (1) {
        if (0 == WIFI_is_configured()) {
            
        }
        else if (0 == WIFI_is_ap_connected()) {
            WIFI_invoke_connection_info();
        }
        else if (0 == WIFI_is_socket_connected()) {
            
        }
        else {
            // test packet send here
            uint8_t cntr = 10;
            while (cntr > 0) {
                debug_printInfo("Pushing packet %d", cntr);
                cntr--;
            }
            WIFI_disconnect_ap_psk();
        }
        
        if (1 == WIFI_has_notif_conn_info()) {
            uint8_t* ip = WIFI_get_ip_address_value();
            printf("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            WIFI_del_notif_conn_info();
        }

        WIFI_invoke_handle_events();
        DELAY_milliseconds(100);
    }


    /*
     * Connect
     */
    debug_printInfo("Create socket connection");

    /*
     * Send 10 packets
     */

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

static void winc_notifier(uint8_t status) {
    // If we have no AP access we want to retry
    if (status != 1) {
    }
}
