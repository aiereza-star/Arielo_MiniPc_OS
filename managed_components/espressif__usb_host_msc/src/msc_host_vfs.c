/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "msc_common.h"
#include "usb/msc_host_vfs.h"
#include "diskio_impl.h"
#include "ffconf.h"
#include "ff.h"
#include "esp_idf_version.h"

#define DRIVE_STR_LEN 3

typedef struct msc_host_vfs {
    char drive[DRIVE_STR_LEN];
    char *base_path;
    uint8_t pdrv;
} msc_host_vfs_t;

static const char *TAG = "MSC VFS";

// ARIELO 01E:
// Mantener la ruta VFS /usb registrada de forma persistente.
// En hot-plug repetido, registrar/desregistrar la ruta puede devolver
// ESP_ERR_NO_MEM aunque haya heap libre, porque el slot VFS/FAT queda
// transitoriamente ocupado. Con esta cache solo se cambia diskio + f_mount.
static bool  s_persist_registered = false;
static BYTE  s_persist_pdrv = 0xFF;
static FATFS *s_persist_fs = NULL;
static char  s_persist_base_path[32] = {0};
static char  s_persist_drive[DRIVE_STR_LEN] = {0};

static bool msc_vfs_persist_match(const char *base_path)
{
    return s_persist_registered &&
           base_path &&
           strcmp(s_persist_base_path, base_path) == 0 &&
           s_persist_pdrv != 0xFF &&
           s_persist_fs != NULL;
}


static esp_err_t msc_format_storage(size_t block_size, size_t allocation_size, const char *drv)
{
    void *workbuf = NULL;
    const size_t workbuf_size = 4096;

    MSC_RETURN_ON_FALSE( workbuf = ff_memalloc(workbuf_size), ESP_ERR_NO_MEM );

    // Valid value of cluster size is between sector_size and 128 * sector_size.
    size_t cluster_size = MIN(MAX(allocation_size, block_size), 128 * block_size);

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    FRESULT err = f_mkfs(drv, FM_ANY | FM_SFD, cluster_size, workbuf, workbuf_size);
#else
    const MKFS_PARM opt = {(BYTE)(FM_ANY | FM_SFD), 0, 0, 0, cluster_size};
    FRESULT err = f_mkfs(drv, &opt, workbuf, workbuf_size);
#endif

    if (err) {
        ESP_LOGE(TAG, "Formatting failed with error: %d", err);
        free(workbuf);
        return ESP_ERR_MSC_FORMAT_FAILED;
    }

    free(workbuf);
    return ESP_OK;
}

esp_err_t msc_host_vfs_format(msc_host_device_handle_t device, const esp_vfs_fat_mount_config_t *mount_config, const msc_host_vfs_handle_t vfs_handle)
{
    MSC_RETURN_ON_INVALID_ARG(device);
    MSC_RETURN_ON_INVALID_ARG(mount_config);
    MSC_RETURN_ON_INVALID_ARG(vfs_handle);

    size_t block_size = ((msc_device_t *)device)->disk.block_size;
    size_t alloc_size = mount_config->allocation_unit_size;

    return msc_format_storage(block_size, alloc_size, vfs_handle->drive);
}

static void dealloc_msc_vfs(msc_host_vfs_t *vfs)
{
    free(vfs->base_path);
    free(vfs);
}

esp_err_t msc_host_vfs_register(msc_host_device_handle_t device,
                                const char *base_path,
                                const esp_vfs_fat_mount_config_t *mount_config,
                                msc_host_vfs_handle_t *vfs_handle)
{
    MSC_RETURN_ON_INVALID_ARG(device);
    MSC_RETURN_ON_INVALID_ARG(base_path);
    MSC_RETURN_ON_INVALID_ARG(mount_config);
    MSC_RETURN_ON_INVALID_ARG(vfs_handle);

    FATFS *fs = NULL;
    BYTE pdrv;
    bool diskio_registered = false;
    bool path_registered_now = false;
    esp_err_t ret = ESP_ERR_MSC_MOUNT_FAILED;
    msc_device_t *dev = (msc_device_t *)device;
    size_t block_size = dev->disk.block_size;
    size_t alloc_size = mount_config->allocation_unit_size;

    msc_host_vfs_t *vfs = calloc(1, sizeof(msc_host_vfs_t));
    MSC_RETURN_ON_FALSE(vfs != NULL, ESP_ERR_NO_MEM);

    if (msc_vfs_persist_match(base_path)) {
        // Ruta /usb ya registrada: reusar FATFS/drive y solo reconectar diskio.
        pdrv = s_persist_pdrv;
        fs = s_persist_fs;
        strncpy(vfs->drive, s_persist_drive, DRIVE_STR_LEN);
        ESP_LOGI(TAG, "ARIELO 01E reuse persistent VFS %s drive=%s",
                 base_path, s_persist_drive);
    } else {
        MSC_GOTO_ON_ERROR( ff_diskio_get_drive(&pdrv) );
        char drive_tmp[DRIVE_STR_LEN] = {(char)('0' + pdrv), ':', 0};
        strncpy(vfs->drive, drive_tmp, DRIVE_STR_LEN);
    }

    ff_diskio_register_msc(pdrv, &dev->disk);
    diskio_registered = true;

    MSC_GOTO_ON_FALSE( vfs->base_path = strdup(base_path), ESP_ERR_NO_MEM );
    vfs->pdrv = pdrv;

    if (!msc_vfs_persist_match(base_path)) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        esp_vfs_fat_conf_t conf = {
            .base_path = base_path,
            .fat_drive = vfs->drive,
            .max_files = mount_config->max_files,
        };
        ret = esp_vfs_fat_register_cfg(&conf, &fs);
#else
        ret = esp_vfs_fat_register(base_path, vfs->drive, mount_config->max_files, &fs);
#endif
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ARIELO 01E esp_vfs_fat_register fallo %s", esp_err_to_name(ret));
            goto fail;
        }
        path_registered_now = true;

        s_persist_registered = true;
        s_persist_pdrv = pdrv;
        s_persist_fs = fs;
        strncpy(s_persist_base_path, base_path, sizeof(s_persist_base_path) - 1);
        s_persist_base_path[sizeof(s_persist_base_path) - 1] = 0;
        strncpy(s_persist_drive, vfs->drive, DRIVE_STR_LEN);
        ESP_LOGI(TAG, "ARIELO 01E persistent VFS registered %s drive=%s",
                 s_persist_base_path, s_persist_drive);
    }

    FRESULT fresult = f_mount(fs, vfs->drive, 1);

    if ( fresult != FR_OK) {
        if (mount_config->format_if_mount_failed &&
                (fresult == FR_NO_FILESYSTEM || fresult == FR_INT_ERR)) {
            MSC_GOTO_ON_ERROR( msc_format_storage(block_size, alloc_size, vfs->drive) );
            MSC_GOTO_ON_FALSE( f_mount(fs, vfs->drive, 0) == FR_OK, ESP_ERR_MSC_MOUNT_FAILED );
        } else {
            ret = ESP_ERR_MSC_MOUNT_FAILED;
            goto fail;
        }
    }

    *vfs_handle = vfs;
    return ESP_OK;

fail:
    if (diskio_registered) {
        ff_diskio_unregister(pdrv);
    }
    if (fs) {
        f_mount(NULL, vfs->drive, 0);
    }

    // Si registramos la ruta ahora y el montaje falló antes de dejarla útil,
    // la desregistramos. Si venía de cache persistente, NO tocamos la ruta.
    if (path_registered_now && !s_persist_registered) {
        esp_vfs_fat_unregister_path(base_path);
    }

    dealloc_msc_vfs(vfs);
    return ret;
}

esp_err_t msc_host_vfs_unregister(msc_host_vfs_handle_t vfs_handle)
{
    MSC_RETURN_ON_INVALID_ARG(vfs_handle);
    msc_host_vfs_t *vfs = (msc_host_vfs_t *)vfs_handle;

    f_mount(NULL, vfs->drive, 0);
    ff_diskio_unregister(vfs->pdrv);

    // ARIELO 01E:
    // NO desregistrar esp_vfs_fat_unregister_path("/usb") en cada hot-unplug.
    // Se mantiene /usb registrado para no consumir/perder slots VFS al reconectar.
    if (!msc_vfs_persist_match(vfs->base_path)) {
        esp_vfs_fat_unregister_path(vfs->base_path);
    } else {
        ESP_LOGI(TAG, "ARIELO 01E persistent VFS kept for %s", vfs->base_path);
    }

    dealloc_msc_vfs(vfs);
    return ESP_OK;
}
