#pragma once
typedef struct {
  int dummy;
} esp_partition_t;
typedef int esp_err_t;
#define ESP_OK 0
extern bool g_bootPartitionSet;
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
