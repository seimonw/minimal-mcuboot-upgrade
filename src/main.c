#include <stdio.h>
#include <app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

#include "zephyr-0.1.0.signed.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define LED0_NODE DT_ALIAS(led0)

#define ISOTP_RX_ID 0x600
#define ISOTP_TX_ID 0x601
#define ISOTP_RX_BUF_SIZE   4095
#define ISOTP_BLOCK_SIZE    8      /* about 64 byte ack blocks */

/* Global variables */
const struct device *can_device;
const int16_t can_id = 0x510;
static struct isotp_recv_ctx recv_ctx;
static struct flash_img_context flash_img_ctx;
static uint8_t rx_data[ISOTP_RX_BUF_SIZE];

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* ISO-TP configuration */
static struct isotp_fc_opts fc_opts = {
    .bs = ISOTP_BLOCK_SIZE,  /* Block size: 64 bytes */
    .stmin = 0,              /* Minimum separation time */
};

static struct isotp_msg_id rx_addr = {
    .std_id = ISOTP_RX_ID,
};

static struct isotp_msg_id tx_addr = {
    .std_id = ISOTP_TX_ID,
};

/* Forward declarations */
static void isotp_recv_work_handler(struct k_work *work);

/* Work queue for handling received data */
static K_WORK_DEFINE(isotp_recv_work, isotp_recv_work_handler);

static void isotp_recv_work_handler(struct k_work *work)
{
    int ret;
    int len;

    /* Receive data */
    len = isotp_recv(&recv_ctx, rx_data, sizeof(rx_data), K_MSEC(100));
    
    if (len > 0) {
        LOG_DBG("Received %d bytes via ISO-TP", len);
        LOG_HEXDUMP_DBG(rx_data, len, "Data");

        /* Write data to flash via flash_img utilities */
        ret = flash_img_buffered_write(&flash_img_ctx, rx_data, len, false);
        if (ret) {
            LOG_ERR("Failed to write to flash: %d", ret);
        } else {
            LOG_DBG("Written %d bytes to update slot", len);
        }
    } else if (len == ISOTP_RECV_TIMEOUT) {
        /* No data available, continue */
    } else if (len < 0) {
        LOG_ERR("ISO-TP receive error: %d", len);
    }

    /* Schedule next receive */
    k_work_submit(&isotp_recv_work);
}

/* This is needed to prevent can_send locking up without a CAN connection */
void can_tx_callback(const struct device *dev, int error, void *user_data)
{
    if (error != 0) {
        LOG_ERR("Failed to send CAN frame: %d", error);
    }
}

static void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    int ret;
    LOG_INF("CAN frame received: ID=0x%03X, DLC=%d", frame->id, frame->dlc);

    if (frame->dlc < 2) {
        LOG_WRN("Invalid frame length: %d", frame->dlc);
        return;
    }

    if (frame->data[0] != '>' || frame->data[1] != 'R') {
        LOG_WRN("Invalid command frame");
        return;
    }

    k_work_cancel(&isotp_recv_work);
    isotp_unbind(&recv_ctx);
    LOG_INF("ISO-TP upgrade disabled");

    // flash_img_check(&flash_img_ctx);
    flash_img_buffered_write(&flash_img_ctx, NULL, 0, true);
    ret = boot_request_upgrade(true);
    if (ret) {
        LOG_ERR("Failed to request upgrade on reboot: %d", ret);
        return;
    }

    sys_reboot(SYS_REBOOT_COLD);
}

/* Initialize CAN bus */
int canbus_init(void)
{
    int ret;

    /* Get CAN device */
    can_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    if (!device_is_ready(can_device)) {
        LOG_ERR("CAN device not ready");
        return -1;
    }

    ret = can_start(can_device);
    if (ret != 0) {
        LOG_ERR("Failed to start CAN: %d", ret);
        return -1;
    }

    /* Setup CAN RX filter for command frames */
    struct can_filter cmd_filter = {
        .id = can_id,
        .mask = CAN_STD_ID_MASK
    };

    ret = can_add_rx_filter(can_device, can_rx_callback, NULL, &cmd_filter);
    if (ret < 0) {
        LOG_ERR("Failed to add CAN RX filter: %d", ret);
        return -1;
    }

    /* Setup ISO-TP */
    /* Initialize flash image context for mcuboot update slot */
    ret = flash_img_init(&flash_img_ctx);
    if (ret) {
        LOG_ERR("Failed to init flash image: %d", ret);
        return ret;
    }

    rx_addr.std_id = ISOTP_RX_ID;
    tx_addr.std_id = ISOTP_TX_ID;

    /* Bind ISO-TP receive context */
    ret = isotp_bind(&recv_ctx, can_device, &rx_addr, &tx_addr, &fc_opts, K_MSEC(100));
    if (ret != ISOTP_N_OK) {
        LOG_ERR("Failed to bind ISO-TP receive: %d", ret);
        return -EIO;
    }

    LOG_INF("ISO-TP upgrade enabled. RX ID: 0x%03X, TX ID: 0x%03X",
        rx_addr.std_id, tx_addr.std_id);
    
    /* Start first receive operation */
    k_work_submit(&isotp_recv_work);

    LOG_DBG("CAN command interface initialized (ID: 0x%03X)", cmd_filter.id);
    return 0;
}



int main(void)
{
    int ret;
    bool led_state = true;

    LOG_INF("Starting minimal mcuboot upgrade application");

    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return 0;
    }

    /* Initialize CAN bus */
    ret = canbus_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize CAN bus: %d", ret);
        return -1;
    }

    struct can_frame status_frame = {
        .id = can_id + 1,
        .dlc = 4,
    };
    /* 32-bit firmware version: MAJOR.MINOR.PATCH.TWEAK */
    sys_put_le32(APPVERSION, status_frame.data);

    ret = can_send(can_device, &status_frame, K_MSEC(100), can_tx_callback, NULL);
    if (ret != 0) {
        LOG_ERR("Failed to send firmware version: %d", ret);
    }
    LOG_INF("Firmware version: %d.%d.%d.%d",
            APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL, APP_TWEAK);

    LOG_INF("All systems initialized, starting main loop");

    /* Start the main command processing loop */
    while (1) {
        ret = gpio_pin_toggle_dt(&led);
        if (ret < 0) {
            return 0;
        }
        
        led_state = !led_state;
        printf("LED state: %s\n", led_state ? "ON" : "OFF");
        k_sleep(K_MSEC(1000));
	LOG_INF("Breaking main loop to force upgrade");
	break;
    }

    int offset = 0;
    int remaining = sizeof(bins_zephyr_0_1_0_signed_bin);
    while (remaining) {
	if (remaining >= 64) {
            ret = flash_img_buffered_write(
                    &flash_img_ctx, &bins_zephyr_0_1_0_signed_bin[offset], 64, false);
	    offset += 64;
	    remaining -= 64;
	} else {
            ret = flash_img_buffered_write(
                    &flash_img_ctx, &bins_zephyr_0_1_0_signed_bin[offset], remaining, false);
	    remaining = 0;
	}

        if (ret) {
            LOG_ERR("Failed to write to flash: %d", ret);
            return ret;
        } else {
            LOG_DBG("Written bytes to update slot");
        }

        k_sleep(K_MSEC(5));
    }

    flash_img_buffered_write(&flash_img_ctx, NULL, 0, true);
    ret = boot_request_upgrade(true);
    if (ret) {
        LOG_ERR("Failed to request upgrade on reboot: %d", ret);
        return ret;
    }

    sys_reboot(SYS_REBOOT_COLD);

    /* This should never be reached */
    return 0;
}
