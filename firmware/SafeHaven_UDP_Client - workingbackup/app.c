/***************************************************************************//**
 * @file
 * @brief SafeHaven microphone-to-UDP audio streaming application
 *
 * Output format:
 *   Sample rate : 16000 Hz
 *   Format      : signed 16-bit PCM
 *   Channels    : mono
 *   UDP port    : 5006
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "sl_status.h"
#include "sl_board_configuration.h"
#include "cmsis_os2.h"

#include "sl_wifi.h"
#include "sl_net.h"
#include "socket.h"
#include "sl_utility.h"
#include "sl_net_si91x.h"
#include "sl_wifi_callback_framework.h"
#include "sl_si91x_driver.h"

#include "sl_si91x_mic.h"
#include "sl_si91x_i2s.h"

#ifdef SLI_SI91X_MCU_INTERFACE
#include "sl_si91x_power_manager.h"
#include "sl_si91x_m4_ps.h"
#endif

/* --------------------------------------------------------------------------
 * Application configuration
 * -------------------------------------------------------------------------- */

#define SAFEHAVEN_MIC_SAMPLE_RATE       16000U
#define SAFEHAVEN_MIC_FRAMES            512U
#define SAFEHAVEN_MIC_CHANNELS          2U
#define SAFEHAVEN_MIC_TIMEOUT_SEC       3U

/* 512 mono samples * 2 bytes per sample = 1024 bytes per UDP packet. */
#define SAFEHAVEN_AUDIO_PACKET_BYTES \
    (SAFEHAVEN_MIC_FRAMES * sizeof(int16_t))

#define SERVER_IP_ADDRESS               "HOST IP ADDRESS HERE"
#define AUDIO_SERVER_PORT               5006U

#define BCAST_FILTER_ENABLE             1U
#define MCAST_FILTER_ENABLE             1U
#define FILTER_MODE                     0U
#define BEACON_DROP_THRESHOLD_MS        5000U

/* --------------------------------------------------------------------------
 * Function declarations
 * -------------------------------------------------------------------------- */

static void application_start(void *argument);
static sl_status_t safehaven_mic_stream(void);

/* --------------------------------------------------------------------------
 * Application thread
 * -------------------------------------------------------------------------- */

const osThreadAttr_t thread_attributes = {
    .name       = "app",
    .attr_bits  = 0,
    .cb_mem     = NULL,
    .cb_size    = 0,
    .stack_mem  = NULL,
    .stack_size = 3072,
    .priority   = osPriorityLow,
    .tz_module  = 0
};

/* --------------------------------------------------------------------------
 * Wi-Fi station configuration
 * -------------------------------------------------------------------------- */

#ifndef SL_SI91X_WC_DEVICE_CONFIGURATION

static const sl_wifi_device_configuration_t station_init_configuration = {
    .boot_option = LOAD_NWP_FW,
    .mac_address = NULL,
    .band        = SL_SI91X_WIFI_BAND_2_4GHZ,

    .boot_config = {
        .oper_mode = SL_SI91X_CLIENT_MODE,
        .coex_mode = SL_SI91X_WLAN_ONLY_MODE,

        .feature_bit_map =
            (SL_WIFI_FEAT_SECURITY_OPEN |
             SL_WIFI_FEAT_AGGREGATION |
             SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE
#ifdef SLI_SI91X_MCU_INTERFACE
             | SL_WIFI_FEAT_WPS_DISABLE
#endif
            ),

        .tcp_ip_feature_bit_map =
            (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT |
             SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),

        .custom_feature_bit_map =
            SL_WIFI_SYSTEM_CUSTOM_FEAT_EXTENSION_VALID,

        .ext_custom_feature_bit_map =
            (SL_WIFI_SYSTEM_EXT_FEAT_LOW_POWER_MODE |
             SL_SI91X_EXT_FEAT_XTAL_CLK |
             SL_SI91X_EXT_FEAT_UART_SEL_FOR_DEBUG_PRINTS |
             MEMORY_CONFIG
#ifdef SLI_SI917
             | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif
            ),

        .bt_feature_bit_map = 0,

        .ext_tcp_ip_feature_bit_map =
            SL_SI91X_CONFIG_FEAT_EXTENSION_VALID,

        .ble_feature_bit_map = 0,
        .ble_ext_feature_bit_map = 0,

        .config_feature_bit_map =
            (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP |
             SL_WIFI_ENABLE_ENHANCED_MAX_PSP)
    },

    .ta_pool = {
        .tx_ratio_in_buffer_pool = 0,
        .rx_ratio_in_buffer_pool = 0,
        .global_ratio_in_buffer_pool = 0
    },

    .efuse_data_type = SL_SI91X_EFUSE_MFG_SW_VERSION,
    .nwp_fw_image_number = SL_SI91X_NWP_FW_IMAGE_NUMBER_0
};

#else

#include "sl_si91x_wc_device_configuration.h"

sl_mac_address_t mac = {
    .octet = SL_SI91X_WC_MAC_ADDRESS
};

static const sl_wifi_device_configuration_t station_init_configuration = {
    .boot_option = SL_SI91X_WC_BOOT_OPTION,

#if SL_SI91X_WC_SET_MAC_ADDRESS
    .mac_address = &mac,
#else
    .mac_address = NULL,
#endif

    .band = SL_SI91X_WC_WIFI_BAND,
    .region_code = SL_SI91X_WC_REGION,

    .boot_config = {
        .oper_mode = SL_SI91X_WC_OPERMODE,
        .coex_mode = SL_SI91X_WC_COEXMODE,
        .feature_bit_map = SL_SI91X_WC_FEATURE_BITMAP,
        .tcp_ip_feature_bit_map = SL_SI91X_WC_TCP_IP_FEATURE_BITMAP,
        .custom_feature_bit_map = SL_SI91X_WC_CUSTOM_FEATURE_BITMAP,
        .ext_custom_feature_bit_map =
            SL_SI91X_WC_EXTENDED_TCPIP_FEATURE_BITMAP,
        .bt_feature_bit_map = SL_SI91X_WC_BLE_FEATURE_BITMAP,
        .ext_tcp_ip_feature_bit_map =
            SL_SI91X_WC_EXTENDED_TCPIP_FEATURE_BITMAP,
        .ble_feature_bit_map = SL_SI91X_WC_BLE_FEATURE_BITMAP,
        .ble_ext_feature_bit_map = SL_SI91X_WC_BLE_EXTENDED_FEATURE_BITMAP,
        .config_feature_bit_map = SL_SI91X_WC_CONFIG_FEATURE_BITMAP
    },

    .ta_pool = {
        .tx_ratio_in_buffer_pool = SL_SI91X_WC_TX_POOL_RATIO,
        .rx_ratio_in_buffer_pool = SL_SI91X_WC_RX_POOL_RATIO,
        .global_ratio_in_buffer_pool = SL_SI91X_WC_GLOBAL_POOL_RATIO
    },

    .efuse_data_type = SL_SI91X_WC_EFUSE_DATA_TYPE,
    .nwp_fw_image_number = SL_SI91X_WC_NWP_FW_IMAGE_NUMBER
};

#endif

/* --------------------------------------------------------------------------
 * Application initialization
 * -------------------------------------------------------------------------- */

void app_init(void)
{
    (void)osThreadNew(
        (osThreadFunc_t)application_start,
        NULL,
        &thread_attributes
    );
}

/* --------------------------------------------------------------------------
 * Microphone capture and UDP streaming
 * -------------------------------------------------------------------------- */

static sl_status_t safehaven_mic_stream(void)
{
    /*
     * Buffer for 512 stereo frames:
     * 512 frames * 2 channels = 1024 int32_t samples.
     *
     * This buffer assumes the I2S driver is configured for 32-bit samples.
     */
    static union {
        int32_t alignment;
        int32_t stereo[SAFEHAVEN_MIC_FRAMES * SAFEHAVEN_MIC_CHANNELS];
    } audio_buffer;

    /*
     * Outgoing audio remains signed 16-bit mono PCM.
     */
    static int16_t mono_buffer[SAFEHAVEN_MIC_FRAMES];

    sl_status_t status = SL_STATUS_OK;
    sl_status_t cleanup_status;

    int32_t socket_fd = -1;
    int32_t bytes_sent;

    struct sockaddr_in destination = { 0 };
    sl_ipv4_address_t server_ip = { 0 };

    uint32_t successful_packets = 0U;

    printf("\r\n[SafeHaven] Starting audio streaming\r\n");
    printf("[SafeHaven] Destination: %s:%u\r\n",
           SERVER_IP_ADDRESS,
           (unsigned int)AUDIO_SERVER_PORT);
    printf("[SafeHaven] Format: 16 kHz, signed 16-bit mono PCM\r\n");
    printf("[SafeHaven] Packet size: %u bytes\r\n",
           (unsigned int)SAFEHAVEN_AUDIO_PACKET_BYTES);

    /* Create the UDP socket. */
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (socket_fd < 0) {
        printf("[SafeHaven] Socket creation failed; errno=%d\r\n", errno);
        return SL_STATUS_FAIL;
    }

    /* Convert the configured laptop IPv4 address. */
    status = sl_net_inet_addr(
        (char *)SERVER_IP_ADDRESS,
        (uint32_t *)&server_ip
    );

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] IP conversion failed: 0x%" PRIx32 "\r\n",
               (uint32_t)status);
        close(socket_fd);
        return status;
    }

    destination.sin_family = AF_INET;
    destination.sin_port = htons(AUDIO_SERVER_PORT);
    destination.sin_addr.s_addr = server_ip.value;

    printf("[SafeHaven] UDP socket ready\r\n");

    while (1) {
        uint32_t start_tick;
        uint32_t timeout_ticks;
        bool mic_initialized = false;
        bool capture_completed = false;

        /* Initialize the microphone. */
        printf("\r\n[DEBUG] Before microphone init\r\n");

        status = sl_si91x_mic_init(
            SAFEHAVEN_MIC_SAMPLE_RATE,
            SAFEHAVEN_MIC_CHANNELS
        );

        printf("[DEBUG] Microphone init status: 0x%" PRIx32 "\r\n",
               (uint32_t)status);

        if (status != SL_STATUS_OK) {
            printf("[SafeHaven] Microphone init failed\r\n");
            break;
        }

        mic_initialized = true;

        /* Clear the capture buffer before requesting samples. */
        memset(audio_buffer.stereo, 0, sizeof(audio_buffer.stereo));

        printf("[DEBUG] Requesting microphone samples\r\n");

        status = sl_si91x_mic_get_n_samples(
            audio_buffer.stereo,
            SAFEHAVEN_MIC_FRAMES
        );

        printf("[DEBUG] Sample request status: 0x%" PRIx32 "\r\n",
               (uint32_t)status);

        if (status != SL_STATUS_OK) {
            printf("[SafeHaven] Sample request failed\r\n");
            goto capture_cleanup;
        }

        timeout_ticks =
            osKernelGetTickFreq() * SAFEHAVEN_MIC_TIMEOUT_SEC;

        start_tick = osKernelGetTickCount();

        /* Wait for the capture-completion flag with a timeout. */
        printf("[DEBUG] Waiting for buffer ready\r\n");

        while (!sl_si91x_mic_sample_buffer_ready()) {
            uint32_t elapsed_ticks =
                (uint32_t)(osKernelGetTickCount() - start_tick);

            if (elapsed_ticks >= timeout_ticks) {
                printf("[SafeHaven] Microphone capture timed out\r\n");
                status = SL_STATUS_TIMEOUT;
                goto capture_cleanup;
            }

            osDelay(1U);
        }

        capture_completed = true;
        printf("[DEBUG] Buffer ready confirmed\r\n");

        /*
         * Select channel 0 from interleaved stereo samples.
         *
         * TEST CONVERSION:
         * This assumes valid microphone sample bits occupy the upper
         * 16 bits of each 32-bit I2S word. Confirm this alignment against
         * the actual SiWx917 I2S receive implementation.
         */
        for (uint32_t i = 0U; i < SAFEHAVEN_MIC_FRAMES; i++) {
            mono_buffer[i] =
                (int16_t)(audio_buffer.stereo[2U * i] >> 16);
        }

        /*
         * Calculate RMS and peak values for debugging.
         */
        {
            uint64_t sum_squares = 0U;
            uint32_t peak = 0U;
            uint32_t rms;
            uint64_t value;
            uint64_t bit = (uint64_t)1U << 62;
            uint64_t root = 0U;

            for (uint32_t i = 0U; i < SAFEHAVEN_MIC_FRAMES; i++) {
                int32_t sample = (int32_t)mono_buffer[i];

                uint32_t magnitude = (sample < 0)
                    ? (uint32_t)(-sample)
                    : (uint32_t)sample;

                sum_squares +=
                    (uint64_t)((int64_t)sample * sample);

                if (magnitude > peak) {
                    peak = magnitude;
                }
            }

            value = sum_squares / SAFEHAVEN_MIC_FRAMES;

            while (bit > value) {
                bit >>= 2;
            }

            while (bit != 0U) {
                if (value >= root + bit) {
                    value -= root + bit;
                    root = (root >> 1) + bit;
                } else {
                    root >>= 1;
                }

                bit >>= 2;
            }

            rms = (uint32_t)root;

            printf("[DEBUG] PCM level: RMS=%" PRIu32
                   ", peak=%" PRIu32 "\r\n",
                   rms, peak);
        }

capture_cleanup:

        /* Stop and deinitialize after each capture attempt. */
        if (mic_initialized) {
            cleanup_status = sl_si91x_mic_stop();

            printf("[DEBUG] Microphone stop status: 0x%" PRIx32 "\r\n",
                   (uint32_t)cleanup_status);

            if ((cleanup_status != SL_STATUS_OK) &&
                (status == SL_STATUS_OK)) {
                status = cleanup_status;
            }

            cleanup_status = sl_si91x_mic_deinit();

            printf("[DEBUG] Microphone deinit status: 0x%" PRIx32 "\r\n",
                   (uint32_t)cleanup_status);

            if ((cleanup_status != SL_STATUS_OK) &&
                (status == SL_STATUS_OK)) {
                status = cleanup_status;
            }
        }

        if (status != SL_STATUS_OK) {
            printf("[SafeHaven] Capture/cleanup failed: 0x%" PRIx32 "\r\n",
                   (uint32_t)status);
            break;
        }

        if (!capture_completed) {
            printf("[SafeHaven] No completed capture; stopping\r\n");
            status = SL_STATUS_FAIL;
            break;
        }

        /* Send signed 16-bit mono PCM to the laptop. */
        bytes_sent = sendto(
            socket_fd,
            (const uint8_t *)mono_buffer,
            (int32_t)SAFEHAVEN_AUDIO_PACKET_BYTES,
            0,
            (const struct sockaddr *)&destination,
            sizeof(destination)
        );

        if (bytes_sent < 0) {
            printf("[SafeHaven] UDP send failed; result=%ld errno=%d\r\n",
                   (long)bytes_sent, errno);
            status = SL_STATUS_FAIL;
            break;
        }

        if ((uint32_t)bytes_sent != SAFEHAVEN_AUDIO_PACKET_BYTES) {
            printf("[SafeHaven] Incomplete UDP packet: %ld bytes\r\n",
                   (long)bytes_sent);
            status = SL_STATUS_FAIL;
            break;
        }

        successful_packets++;

        if ((successful_packets % 20U) == 0U) {
            printf("[SafeHaven] Successfully sent %" PRIu32
                   " audio packets\r\n",
                   successful_packets);
        }
    }

    if (socket_fd >= 0) {
        close(socket_fd);
    }

    printf("[SafeHaven] Audio stream stopped\r\n");

    return status;
}

/* --------------------------------------------------------------------------
 * Main application task
 * -------------------------------------------------------------------------- */

static void application_start(void *argument)
{
    UNUSED_PARAMETER(argument);

    sl_status_t status;

    sl_wifi_performance_profile_v2_t performance_profile = {
        .profile = ASSOCIATED_POWER_SAVE_LOW_LATENCY
    };

    sl_si91x_firmware_version_t version = { 0 };
    sl_mac_address_t mac_addr = { 0 };

    printf("\r\n[SafeHaven] Initializing Wi-Fi\r\n");

    status = sl_net_init(
        SL_NET_WIFI_CLIENT_INTERFACE,
        &station_init_configuration,
        NULL,
        NULL
    );

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] Wi-Fi initialization failed: 0x%" PRIx32 "\r\n",
               (uint32_t)status);
        return;
    }

    status = sl_wifi_get_mac_address(
        SL_WIFI_CLIENT_INTERFACE,
        &mac_addr
    );

    if (status == SL_STATUS_OK) {
        printf("[SafeHaven] MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
               mac_addr.octet[0],
               mac_addr.octet[1],
               mac_addr.octet[2],
               mac_addr.octet[3],
               mac_addr.octet[4],
               mac_addr.octet[5]);
    } else {
        printf("[SafeHaven] MAC address read failed: 0x%" PRIx32 "\r\n",
               (uint32_t)status);
    }

    status = sl_si91x_get_firmware_version(&version);

    if (status == SL_STATUS_OK) {
        printf("[SafeHaven] Firmware: %x%x.%d.%d.%d.%d.%d.%d\r\n",
               version.chip_id,
               version.rom_id,
               version.major,
               version.minor,
               version.security_version,
               version.patch_num,
               version.customer_id,
               version.build_num);
    } else {
        printf("[SafeHaven] Firmware version read failed: 0x%" PRIx32 "\r\n",
               (uint32_t)status);
    }

    /* Connect using the Wi-Fi profile configured in the project. */
    status = sl_net_up(
        SL_NET_WIFI_CLIENT_INTERFACE,
        SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID
    );

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] Wi-Fi connection failed: 0x%" PRIx32 "\r\n",
               (uint32_t)status);
        return;
    }

    printf("[SafeHaven] Wi-Fi client connected\r\n");

    /* Configure groupcast filtering. */
    {
        sl_wifi_groupcast_filter_config_t filter_config = { 0 };

        filter_config.enable_bcast_filter =
            (uint8_t)BCAST_FILTER_ENABLE;
        filter_config.enable_mcast_filter =
            (uint8_t)MCAST_FILTER_ENABLE;
        filter_config.filter_mode = (uint8_t)FILTER_MODE;

        status = sl_wifi_set_groupcast_filter_config(&filter_config);

        if (status != SL_STATUS_OK) {
            printf("[SafeHaven] Groupcast filter setup failed: 0x%" PRIx32
                   "\r\n", (uint32_t)status);
            return;
        }
    }

    status = sl_wifi_set_beacon_drop_threshold(
        SL_WIFI_CLIENT_INTERFACE,
        (uint16_t)BEACON_DROP_THRESHOLD_MS
    );

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] Beacon threshold setup failed: 0x%" PRIx32
               "\r\n", (uint32_t)status);
        return;
    }

    status = sl_wifi_set_performance_profile_v2(&performance_profile);

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] Performance profile setup failed: 0x%" PRIx32
               "\r\n", (uint32_t)status);
        return;
    }

    printf("[SafeHaven] Starting microphone streaming\r\n");

    status = safehaven_mic_stream();

    if (status != SL_STATUS_OK) {
        printf("[SafeHaven] Microphone streaming exited: 0x%" PRIx32
               "\r\n", (uint32_t)status);
    }

#ifdef SLI_SI91X_MCU_INTERFACE
    /*
     * Keep the application task alive after streaming exits.
     */
    {
        osSemaphoreId_t wait_semaphore = osSemaphoreNew(1U, 0U, NULL);

        if (wait_semaphore == NULL) {
            printf("[SafeHaven] Failed to create wait semaphore\r\n");
            return;
        }

        (void)osSemaphoreAcquire(wait_semaphore, osWaitForever);
    }
#endif
}