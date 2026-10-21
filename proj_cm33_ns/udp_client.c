/******************************************************************************
 * File Name:   udp_client.c
 *
 * Description: This file contains task and functions related to UDP client
 *              operation.
 *
 ********************************************************************************
 * (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 *******************************************************************************/

/* Header file includes. */
#include "cybsp.h"
#include <inttypes.h>
/* FreeRTOS header file. */
#include <FreeRTOS.h>
#include <task.h>
#include "cy_secure_sockets.h"
/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"
#include "cy_wcm_error.h"
/* UDP client task header file. */
#include "udp_client.h"
#include "retarget_io_init.h"
/*******************************************************************************
 * Macros
 ********************************************************************************/
/* LED ON, LED OFF and Invalid commands. */
#define LED_OFF_CMD                        '0'
#define LED_ON_CMD                         '1'
#define INVALID_CMD                        '2'

/* Acknowledgment messages to be sent UDP Server. */
#define ACK_LED_ON                         "LED ON ACK"
#define ACK_LED_OFF                        "LED OFF ACK"
#define INVALID_CMD_MSG                    "INVALID CMD RECEIVED"
#define SHIFT_8BITS                        (8U)
#define SHIFT_16BITS                       (16U)
#define SHIFT_24BITS                       (24U)
#define INIT_ZERO_VALUE                    (0U)

/* Initial message sent to UDP Server to confirm client availability. */
#define START_COMM_MSG                         "A"

/* Buffer size to store the incoming messages from server, in bytes. */
#define MAX_UDP_RECV_BUFFER_SIZE           (20U)

/* RTOS related macros for UDP client task. */
#define RTOS_TASK_TICKS_TO_WAIT            (1000U)
#define APP_SDIO_INTERRUPT_PRIORITY        (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY   (2U)
#define APP_SDIO_FREQUENCY_HZ              (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK             (64U)


/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/

/*******************************************************************************
 * Global Variables
 ********************************************************************************/
/* UDP client socket handle */
cy_socket_t client_handle;
cy_socket_sockaddr_t peer_addr;

/* UDP Client task handle. */
extern TaskHandle_t client_task_handle;
static mtb_hal_sdio_t sdio_instance;
cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* SysPm callback parameter structure for SDHC */
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
    .context   = &sdhc_host_context,
    .base      = CYBSP_WIFI_SDIO_HW
};

/* SysPm callback structure for SDHC*/
static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback           = Cy_SD_Host_DeepSleepCallback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &sdcardDSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};
#endif

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: sdio_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for SDIO instance.
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

/*******************************************************************************
* Function Name: host_wake_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for the host wake up input pin.
*******************************************************************************/
static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
* Function Name: app_sdio_init
********************************************************************************
* Summary:
* This function configures and initializes the SDIO instance used in
* communication between the host MCU and the wireless device.
*******************************************************************************/
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
            .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
            .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    /* Initialize the SDIO interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg, sdio_interrupt_handler);

    /* SDIO interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO using the HAL object and desired configuration */
    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config, NULL, &sdhc_host_context);

    /* SDIO setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize and Enable SD HOST */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.host_config, &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    /* Configure SDIO */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    /* SDHC SysPm callback registration */
    Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);
#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

    /* Setup GPIO using the HAL object for WIFI WL REG ON  */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM, CYBSP_WIFI_WL_REG_ON_PIN);

    /* Setup GPIO using the HAL object for WIFI HOST WAKE PIN  */
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin, CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize the Host wakeup interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status_host_wake =  Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    /* Host wake up interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

/*******************************************************************************
 * Function Name: udp_client_recv_handler
 *******************************************************************************
 * Summary:
 *  Callback function to handle incoming UDP server messages.
 *
 * Parameters:
 *  cy_socket_t socket_handle: Connection handle for the UDP client socket
 *  void *args : Parameter passed on to the function (unused)
 *
 * Return:
 *  cy_result result: Result of the operation
 *
 *******************************************************************************/
static cy_rslt_t udp_client_recv_handler(cy_socket_t socket_handle, void *arg)
{
    cy_rslt_t result;
    /* Variable to store the number of bytes received. */
    uint32_t bytes_received = 0;
    /* Buffer to store received data. */
    char rx_buffer[MAX_UDP_RECV_BUFFER_SIZE] = {0};

    /* Receive incoming message from UDP server. */
    result = cy_socket_recvfrom(client_handle, rx_buffer, MAX_UDP_RECV_BUFFER_SIZE,
            CY_SOCKET_FLAGS_RECVFROM_NONE, NULL, INIT_ZERO_VALUE,
            &bytes_received);

    /* Send notification to the task to turn the LED on/off and send acknowledgment to the server. */
    xTaskNotify(client_task_handle, (uint32_t)rx_buffer[0], eSetValueWithoutOverwrite);

    return result;
}

/*******************************************************************************
 * Function Name: create_udp_client_socket
 *******************************************************************************
 * Summary:
 *  Function to create a socket and set the socket options
 *  to set callback function for handling incoming messages.
 *
 *  Parameters
 *  void
 *
 *  Return
 *  cy_rslt_t : Result of the operation
 *******************************************************************************/
static cy_rslt_t create_udp_client_socket(void)
{
    cy_rslt_t result;

    /* Create a UDP socket. */
    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, CY_SOCKET_TYPE_DGRAM,
            CY_SOCKET_IPPROTO_UDP, &client_handle);

    if(CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    /* Variable used to set socket receive callback function. */
    cy_socket_opt_callback_t udp_recv_option = {
            .callback = udp_client_recv_handler,
            .arg = NULL
    };

    /* Register the callback function to handle messages received from UDP client. */
    result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET,
            CY_SOCKET_SO_RECEIVE_CALLBACK,
            &udp_recv_option, sizeof(cy_socket_opt_callback_t));

    return result;
}

/*******************************************************************************
 * Function Name: connect_to_wifi_ap()
 *******************************************************************************
 * Summary:
 *  Connects to Wi-Fi AP using the user-configured credentials, retries up to a
 *  configured number of times until the connection succeeds.
 *
 *  Parameters
 *  void
 *
 *  Return
 *  cy_rslt_t : Result of the operation
 *******************************************************************************/
static cy_rslt_t connect_to_wifi_ap(void)
{
    cy_rslt_t result;

    /* Variables used by Wi-Fi connection manager.*/
    cy_wcm_connect_params_t wifi_conn_param;

    cy_wcm_ip_address_t ip_address;

    app_sdio_init();

    /* Initialize wcm */
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    /* Initialize Wi-Fi connection manager. */
    result = cy_wcm_init(&wcm_config);

    if (CY_RSLT_SUCCESS != result)
    {
        printf("Wi-Fi Connection Manager Initialization Failed.\r\n");
        handle_app_error();
    }
    printf("Wi-Fi Connection Manager initialized.\r\n");

    /* Set the Wi-Fi SSID, password and security type. */
    memset(&wifi_conn_param, INIT_ZERO_VALUE, sizeof(cy_wcm_connect_params_t));
    memcpy(wifi_conn_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_conn_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifi_conn_param.ap_credentials.security = WIFI_SECURITY_TYPE;

    /* Join the Wi-Fi AP. */
    for(uint32_t conn_retries = 0; conn_retries < MAX_WIFI_CONN_RETRIES; conn_retries++ )
    {
        result = cy_wcm_connect_ap(&wifi_conn_param, &ip_address);

        if(CY_RSLT_SUCCESS == result)
        {
            printf("Successfully connected to Wi-Fi network '%s'.\n",
                    wifi_conn_param.ap_credentials.SSID);
            printf("IP Address Assigned: %d.%d.%d.%d\n", (uint8)ip_address.ip.v4,
                    (uint8)(ip_address.ip.v4 >> SHIFT_8BITS), (uint8)(ip_address.ip.v4 >> SHIFT_16BITS),
                    (uint8)(ip_address.ip.v4 >> SHIFT_24BITS));
            return result;
        }

        printf("Connection to Wi-Fi network failed with error code %d."
                "Retrying in %d ms...\n", (int)result, WIFI_CONN_RETRY_INTERVAL_MSEC);

        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_INTERVAL_MSEC));
    }

    /* Stop retrying after maximum retry attempts. */
    printf("Exceeded maximum Wi-Fi connection attempts\n");

    return result;
}

/*******************************************************************************
 * Function Name: udp_client_task
 *******************************************************************************
 * Summary:
 *  Task used to create a UDP Client and communicate with remote UDP Server to
 *  control the LED state (ON/OFF) based on command received from UDP server.
 *
 * Parameters:
 *  void *args : Task parameter defined during task creation (unused).
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void udp_client_task(void *arg)
{
    cy_rslt_t result ;
    uint8_t tasknotifybits_to_clearonentry = 0;
    uint8_t tasknotifybits_to_clearonexit = 0;

    /* Variable to store the number of bytes sent to the UDP server. */
    uint32_t bytes_sent = 0;

    /* Variable to receive LED ON/OFF from udp_client_recv_handler. */
    uint32_t led_state_ack = LED_OFF_CMD;

    /* IP address and UDP port number of the UDP server */
    cy_socket_sockaddr_t udp_server_addr = {
            .ip_address.ip.v4 = UDP_SERVER_IP_ADDRESS,
            .ip_address.version = CY_SOCKET_IP_VER_V4,
            .port = UDP_SERVER_PORT
    };

    /* Connect to Wi-Fi AP */
    if(CY_RSLT_SUCCESS != connect_to_wifi_ap())
    {
        printf("\n Failed to connect to Wi-FI AP.\n");
        handle_app_error();
    }

    /* Secure Sockets initialized */
    result = cy_socket_init();

    if (CY_RSLT_SUCCESS != result)
    {
        printf("Secure Sockets initialization failed!\n");
        handle_app_error();
    }

    printf("Secure Sockets initialized\n");

    result = create_udp_client_socket();

    if (CY_RSLT_SUCCESS != result)
    {
        printf("UDP Client Socket creation failed!\n");
        handle_app_error();
    }

    /* First send data to Server and wait to receive command */
    result = cy_socket_sendto(client_handle, START_COMM_MSG, strlen(START_COMM_MSG),
            CY_SOCKET_FLAGS_NONE,&udp_server_addr,
            sizeof(cy_socket_sockaddr_t), &bytes_sent);

    if(CY_RSLT_SUCCESS == result)
    {
        printf("Data sent to server\n");
    }
    else
    {
        printf("Failed to send data to server. Error : %"PRIu32"\n", result);
    }

    while(true)
    {
        /* Wait till ON/OFF command is received from UDP Server . */
        xTaskNotifyWait(tasknotifybits_to_clearonentry, tasknotifybits_to_clearonexit,
                &led_state_ack, portMAX_DELAY);

        printf("============================================================\n");

        /* Send acknowledgment to server after setting the LED ON or OFF */
        if(LED_ON_CMD == led_state_ack)
        {
            /* Turn the LED ON and set flag to send acknowledgment  */
            printf("Command received from server to turn on LED\n");
            Cy_GPIO_Set(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
            printf("LED turned ON\n");

            result = cy_socket_sendto(client_handle, ACK_LED_ON, strlen(ACK_LED_ON),
                    CY_SOCKET_FLAGS_NONE,&udp_server_addr,
                    sizeof(cy_socket_sockaddr_t), &bytes_sent);

            if(CY_RSLT_SUCCESS != result)
            {
                printf("Failed to send Acknowledgment to server. Error: %"PRIu32"\n", result);
            }
        }
        else if(LED_OFF_CMD == led_state_ack)
        {
            /* Turn the LED OFF and set flag to send acknowledgment */
            printf("Command received from server to turn off LED\n");
            Cy_GPIO_Clr(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
            printf("LED turned OFF\n");

            result = cy_socket_sendto(client_handle, ACK_LED_OFF, strlen(ACK_LED_OFF),
                    CY_SOCKET_FLAGS_NONE,&udp_server_addr,
                    sizeof(cy_socket_sockaddr_t), &bytes_sent);

            if(CY_RSLT_SUCCESS != result)
            {
                printf("Failed to send acknowledgment to server. Error: %"PRIu32"\n", result);
            }
        }
        else
        {
            printf("Invalid command received.\n");
            result = cy_socket_sendto(client_handle, INVALID_CMD_MSG, strlen(INVALID_CMD_MSG),
                    CY_SOCKET_FLAGS_NONE, &udp_server_addr,
                    sizeof(cy_socket_sockaddr_t), &bytes_sent);

            if(CY_RSLT_SUCCESS != result)
            {
                printf("Failed to send acknowledgment to server. Error: %"PRIu32"\n", result);
            }
        }
    }
}

/* [] END OF FILE */
