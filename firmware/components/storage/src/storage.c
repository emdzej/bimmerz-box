#include "storage.h"

#include "boards/board.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "storage";
static const char *MOUNT_POINT = "/sdcard";

static sdmmc_card_t *s_card = NULL;

esp_err_t storage_mount(void) {
    if (s_card != NULL) {
        ESP_LOGW(TAG, "already mounted");
        return ESP_OK;
    }

    if (BOARD_SD_CLK_GPIO < 0) {
        ESP_LOGW(TAG, "no SD card pins configured for this board — skipping mount");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "mounting SD card on SDMMC slot 0 (4-bit) at %s", MOUNT_POINT);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // SDMMC_HOST_DEFAULT() picks slot 1 — that's what ESP-Hosted uses
    // for the C6 link. The SD card on this board is on slot 0 (the
    // dedicated IO-MUX pins 43/44/39-42). Without this override, our
    // mount call writes over ESP-Hosted's slot-1 state and the C6 link
    // crashes on its first RPC.
    host.slot = SDMMC_HOST_SLOT_0;
    // Default is SDMMC_FREQ_DEFAULT = 20 MHz (probing-safe). Every
    // modern card supports SDMMC_FREQ_HIGHSPEED = 40 MHz over the
    // 4-bit bus, doubling read throughput on the app-serve path
    // (dashboard + `/sdcard/apps/<slug>/` static assets). If the card
    // negotiation fails at 40 MHz, ESP-IDF's sdmmc_host_do_slot_init
    // falls back to a slower rate automatically — safe to try. UHS-I
    // (SDR104 @ 100+ MHz) would give another ~2× on top but requires
    // card-side compat and 1.8 V signaling, out of scope here.
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

#if SOC_SDMMC_IO_POWER_EXTERNAL && defined(BOARD_SD_LDO_IO_CHANNEL)
    // The Waveshare ESP32-P4 Module DEV-KIT powers the SD card from the
    // P4's internal LDO IO. Without this, the card never sees Vcc and
    // SDMMC bus init fails. See docs in
    // managed_components/.../sd_pwr_ctrl_by_on_chip_ldo.h.
    sd_pwr_ctrl_ldo_config_t ldo_cfg = {
        .ldo_chan_id = BOARD_SD_LDO_IO_CHANNEL,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
    esp_err_t lerr = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl);
    if (lerr != ESP_OK) {
        ESP_LOGE(TAG, "sd_pwr_ctrl_new_on_chip_ldo failed: %s",
                 esp_err_to_name(lerr));
        return ESP_OK;  // tolerated; boot continues
    }
    host.pwr_ctrl_handle = pwr_ctrl;
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = BOARD_SD_CLK_GPIO;
    slot.cmd   = BOARD_SD_CMD_GPIO;
    slot.d0    = BOARD_SD_D0_GPIO;
    slot.d1    = BOARD_SD_D1_GPIO;
    slot.d2    = BOARD_SD_D2_GPIO;
    slot.d3    = BOARD_SD_D3_GPIO;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s — boot will continue without SD",
                 esp_err_to_name(err));
        s_card = NULL;
        // Tolerated: bench dev without an SD card inserted should still
        // come up far enough to expose status over USB-C console.
        return ESP_OK;
    }

    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t storage_unmount(void) {
    if (s_card == NULL) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    return err;
}

const char *storage_mount_point(void) {
    return MOUNT_POINT;
}
