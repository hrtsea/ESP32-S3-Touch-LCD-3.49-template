#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdcard_bsp.h"
#include "esp_log.h"
#include "esp_err.h"

#define SDMMC_D0_PIN    40  
#define SDMMC_CLK_PIN   41
#define SDMMC_CMD_PIN   39

static const char *TAG = "_sdcard";

EventGroupHandle_t sdcard_even_ = NULL;

sdcard_bsp_t user_sdcard_bsp;

#define SDlist "/sdcard" //目录,类似于一个标准

sdmmc_card_t *card_host = NULL;

void _sdcard_init(void)
{
  sdcard_even_ = xEventGroupCreate();
  esp_vfs_fat_sdmmc_mount_config_t mount_config =
  {
    /* Don't auto-format on mount failure: a half-formatted or
       hot-removed card can keep the format routine looping for
       minutes, blocking the UI. The Storage settings page exposes a
       "Format SD card" button for explicit reformat. */
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
  };

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  slot_config.clk = SDMMC_CLK_PIN;
  slot_config.cmd = SDMMC_CMD_PIN;
  slot_config.d0 = SDMMC_D0_PIN;
  /* SOC_SDMMC_USE_GPIO_MATRIX boards (ESP32-S3 with SDMMC routed through
     the GPIO matrix) need internal pull-ups on CMD/D0 unless the board
     has external ones. Without this, mount fails with EACCES (errno 13)
     because the card never responds correctly on CMD. */
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  slot_config.cd = SDMMC_SLOT_NO_CD;
  slot_config.wp = SDMMC_SLOT_NO_WP;

  esp_err_t mr = esp_vfs_fat_sdmmc_mount(SDlist, &host, &slot_config, &mount_config, &card_host);
  if (mr != ESP_OK) {
    ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(mr));
    /* esp_vfs_fat_sdmmc_mount can leave card_host set even on failure.
       Force NULL so callers don't poll a dead card. */
    card_host = NULL;
    return;
  }
  if (card_host != NULL) {
    sdmmc_card_print_info(stdout, card_host);
    user_sdcard_bsp.sdcard_size = (float)(card_host->csd.capacity)/2048/1024;
    xEventGroupSetBits(sdcard_even_, 0x01);
  }
}

bool sdcard_is_mounted(void)
{
  return card_host != NULL && !sdcard_busy;
}

/* Visible to other components that touch /sdcard via opendir/statvfs/etc.
   They should bail when this is set to avoid spamming "Failed to get
   number of free clusters" while the volume is unmounted. */
volatile bool sdcard_busy = false;

esp_err_t sdcard_format(void)
{
  if (card_host == NULL) {
    ESP_LOGE(TAG, "format: card not mounted");
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGW(TAG, "formatting %s -- this can take 30-90s on a 64GB card", SDlist);
  sdcard_busy = true;
  esp_err_t r = esp_vfs_fat_sdcard_format(SDlist, card_host);
  sdcard_busy = false;
  ESP_LOGI(TAG, "format -> %s", esp_err_to_name(r));
  return r;
}



/* Write data
path: Path
data: Data */ 
esp_err_t sdcard_file_write(const char *path, const char *data)
{
  esp_err_t err;
  if(card_host == NULL)
  {
    ESP_LOGE(TAG, "SD card not initialized (card == NULL)");
    return ESP_ERR_NOT_FOUND;
  }
  err = sdmmc_get_status(card_host); //First, check if there is an SD card.
  if(err != ESP_OK)
  {
    ESP_LOGE(TAG, "SD card status check failed (card not present or unresponsive)");
    return err;
  }
  FILE *f = fopen(path, "w"); //Obtain the path address
  if(f == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file: %s", path);
    return ESP_ERR_NOT_FOUND;
  }
  fprintf(f, data); 
  fclose(f);
  return ESP_OK;
}
/*
Read data
path: path */
esp_err_t sdcard_file_read(const char *path, char *buffer, size_t *out_len)
{
  esp_err_t err;
  if(card_host == NULL)
  {
    ESP_LOGE(TAG, "SD card not initialized (card == NULL)");
    return ESP_ERR_NOT_FOUND;
  }
  err = sdmmc_get_status(card_host); //First, check if there is an SD card.
  if(err != ESP_OK)
  {
    ESP_LOGE(TAG, "SD card status check failed (card not present or unresponsive)");
    return err;
  }
  FILE *f = fopen(path, "rb");
  if (f == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file: %s", path);
    return ESP_ERR_NOT_FOUND;
  }
  fseek(f, 0, SEEK_END);     //Move the pointer to the very end.
  uint32_t unlen = ftell(f);
  //fgets(pxbuf, unlen, f); //read characters from file
  fseek(f, 0, SEEK_SET); //Move the pointer to the very beginning.
  uint32_t poutLen = fread((void *)buffer,1,unlen,f);
  if(out_len != NULL)
  *out_len = poutLen;
  fclose(f);
  return ESP_OK;
}