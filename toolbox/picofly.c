/*
 * Copyright (c) 2021 HWFLY
 * Picofly support added by DefenderOfHyrule
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modchip toolbox, RP2040-based modchip support.
 *
 * communication protocol overview
 * --------------------------------
 * the RP2040 presents itself as an eMMC device wired to the
 * Switch's eMMC bus.  to send a command to the modchip the
 * payload writes a 64-word (u32) command packet to BOOT0 sector 1
 * (byte offset 512).  the firmware binary, when required, is written
 * to BOOT0 starting at sector 2 (PICOFLY_FW_START_SECTOR).
 *
 * command packet layout
 * ---------------------
 *   word[0]  : opcode / magic
 *   word[1]  : arg0  (for fw update: start_block = PICOFLY_FW_START_SECTOR)
 *   word[2]  : arg1  (for fw update: size in 512-byte blocks)
 *   word[3..63]: must be zero
 *
 * supported commands
 * ------------------
 *   training-data reset : opcode=0x515205C5, arg0=0, arg1=0
 *   firmware update     : opcode=0x6DB92148, arg0=PICOFLY_FW_START_SECTOR(2),
 *                         arg1=(update.bin size rounded up / 512)
 *   firmware rollback   : opcode=0x6DB92148, arg0=0xFFFFFFFF,
 *                         arg1=0xFFFFFFFF
 *
 * firmware binary format: fw_header { uint32_t size; uint32_t crc; uint8_t data[]; }
 * 0x9cabe959 is the descriptor signature picofly WRITES to BOOT0 block 0x1FFF;
 * it is NOT a firmware file header.
 */

#include <string.h>
#include <stdlib.h>
#include <bdk.h>

#include "gfx/tui.h"
#include "picofly.h"

// internal helpers

/**
 * _picofly_write_cmd, build and write a 64-word command block to BOOT0 sector 1.
 *
 * @opcode : command opcode (word[0])
 * @arg0   : first argument (word[1])
 * @arg1   : second argument (word[2])
 *
 * returns 0 on success, non-zero on eMMC write failure.
 */
static int _picofly_write_cmd(u32 opcode, u32 arg0, u32 arg1)
{
    /* allocate one sector worth of buffer, zero filled. */
    u32 cmd[PICOFLY_CMD_WORDS];
    memset(cmd, 0, sizeof(cmd));

    cmd[0] = opcode;
    cmd[1] = arg0;
    cmd[2] = arg1;

    /* write to BOOT0 sector 1 (second sector). */
    if (!sdmmc_storage_write(&emmc_storage, PICOFLY_CMD_SECTOR, 1, cmd))
    {
        gfx_printf("%kFailed to write command to BOOT0!\n%k", 0xFFFF0000, 0xFFCCCCCC);
        return 1;
    }
    return 0;
}

/**
 * _picofly_write_fw, write firmware binary to BOOT0 starting at sector 2.
 *
 * @fw   : pointer to firmware data
 * @size : firmware size in bytes (will be rounded up to a sector boundary)
 *
 * returns 0 on success, non zero on failure.
 */
static int _picofly_write_fw(const u8 *fw, u32 size)
{
    /* round size up to the next sector boundary. */
    u32 sectors = (size + PICOFLY_SECTOR_SZ - 1) / PICOFLY_SECTOR_SZ;

    /*
     * allocate an aligned buffer large enough to hold the full firmware
     * rounded to a sector multiple.
     */
    u32 aligned_size = sectors * PICOFLY_SECTOR_SZ;
    u8 *buf = (u8 *)malloc(aligned_size);
    if (!buf)
    {
        gfx_printf("%kOut of memory!\n%k", 0xFFFF0000, 0xFFCCCCCC);
        return 1;
    }

    memset(buf, 0xFF, aligned_size);     /* pad with 0xFF (erased flash value) */
    memcpy(buf, fw, size);

    /* firmware starts at PICOFLY_FW_START_SECTOR (sector 2) of BOOT0. */
    u32 start_sector = PICOFLY_FW_START_SECTOR;

    u32 sectors_remaining = sectors;
    u32 offset = 0;

    while (sectors_remaining)
    {
        /* write up to 128 sectors at a time to avoid huge single transfers. */
        u32 chunk = sectors_remaining < 128 ? sectors_remaining : 128;

        if (!sdmmc_storage_write(&emmc_storage, start_sector + offset, chunk, buf + offset * PICOFLY_SECTOR_SZ))
        {
            gfx_printf("%kFirmware write failed at sector %d!\n%k",
                       0xFFFF0000, start_sector + offset, 0xFFCCCCCC);
            free(buf);
            return 1;
        }

        /* progress bar */
        u8 pct = (u8)(((u64)(offset + chunk) * 100) / sectors);
        tui_pbar(0, gfx_con.y, pct, 0xFFFF9600, 0xFF551500);

        offset            += chunk;
        sectors_remaining -= chunk;
    }

    tui_pbar(0, gfx_con.y, 100, 0xFFFF9600, 0xFF551500);
    free(buf);
    return 0;
}

// public API

/**
 * picofly_update_fw, flash a new firmware to the RP2040.
 *
 * reads "update.bin" from the SD card root, validates the signature,
 * writes the binary to BOOT0 followed by the firmware-update command.
 *
 * returns 0 on success, non zero on failure.
 */
int picofly_update_fw(void)
{
    int res = 1;

    gfx_printf("Mounting SD card...\n");
    if (!sd_mount())
    {
        gfx_printf("%kFailed to mount SD card!\n%k", 0xFFFF0000, 0xFFCCCCCC);
        return 1;
    }

    gfx_printf("Reading update.bin...\n");
    u32 fw_size = 0;
    u8 *fw = sd_file_read("update.bin", &fw_size);
    sd_end();

    if (!fw)
    {
        gfx_printf("%kupdate.bin not found on SD card!\n%k", 0xFFFF0000, 0xFFCCCCCC);
        return 1;
    }

    if (fw_size < 4)
    {
        gfx_printf("%kFirmware file too small!\n%k", 0xFFFF0000, 0xFFCCCCCC);
        free(fw);
        return 1;
    }

    if (fw_size > PICOFLY_FW_MAX_SIZE)
    {
        gfx_printf("%kFirmware file too large (%d bytes, max %d)!\n%k",
                   0xFFFF0000, fw_size, PICOFLY_FW_MAX_SIZE, 0xFFCCCCCC);
        free(fw);
        return 1;
    }

    gfx_printf("Firmware size: %d bytes (%d sectors)\n",
               fw_size, (fw_size + PICOFLY_SECTOR_SZ - 1) / PICOFLY_SECTOR_SZ);

    /*
     * note: The firmware binary uses the fw_header format:
     *   uint32_t size; uint32_t crc; uint8_t data[];
     * no magic signature validation is performed here; the chip
     * validates using CRC during programming.
     */

    gfx_printf("\nPress Power to flash or VOL to cancel...\n");

    msleep(500);
    u32 btn = btn_wait();
    if (btn & (BTN_VOL_UP | BTN_VOL_DOWN))
    {
        gfx_printf("Cancelled.\n");
        free(fw);
        return 0;
    }

    /* initialize eMMC for BOOT0 access. */
    gfx_printf("\nInitialising eMMC...\n");
    emmc_initialize(false);
    sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);

    /* step 1: write firmware binary to BOOT0 (starting sector 2). */
    gfx_printf("Writing firmware to BOOT0...\n");
    if (_picofly_write_fw(fw, fw_size))
        goto cleanup;

    gfx_printf("\nFirmware written successfully.\n");

    /* step 2: write the update command to BOOT0 sector 1.
     *   opcode = 0x6DB92148
     *   arg0   = 0x1f80
     *   arg1   = ceil(fw_size / 512)
     */
    u32 size_in_sectors = (fw_size + PICOFLY_SECTOR_SZ - 1) / PICOFLY_SECTOR_SZ;
    gfx_printf("Writing firmware update command\n(size_sectors=%d)...\n", size_in_sectors);

    if (_picofly_write_cmd(PICOFLY_OPCODE_FW_CMD, PICOFLY_ARG_FW_UPDATE, size_in_sectors))
        goto cleanup;

    gfx_printf("%kUpdate command sent successfully!\n%k", 0xFF00FF00, 0xFFCCCCCC);
    gfx_printf("%kUpdating firmware...\n%k", 0xFF00FF00, 0xFFCCCCCC);
    res = 0;

cleanup:
    sdmmc_storage_end(&emmc_storage);
    free(fw);
    return res;
}

/**
 * picofly_rollback_fw, instruct the RP2040 to roll back to the previous firmware.
 *
 * writes the rollback command (arg0=0xFFFFFFFF, arg1=0xFFFFFFFF) to BOOT0.
 *
 * returns 0 on success, non zero on failure.
 */
int picofly_rollback_fw(void)
{
    gfx_printf("Initialising eMMC...\n");
    emmc_initialize(false);
    sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);

    gfx_printf("Writing firmware rollback command...\n");
    int res = _picofly_write_cmd(PICOFLY_OPCODE_FW_CMD, PICOFLY_ARG_FW_ROLLBACK, PICOFLY_ARG_FW_ROLLBACK);

    sdmmc_storage_end(&emmc_storage);

    if (!res)
    {
        gfx_printf("%kRollback command sent successfully!\n%k", 0xFF00FF00, 0xFFCCCCCC);
        gfx_printf("Reboot the console to complete the rollback.\n");
    }
    return res;
}

/**
 * picofly_reset_train_data, reset picofly training data.
 *
 * writes the training-reset command to BOOT0 sector 1.
 * on the next boot the modchip will retrain its glitch parameters.
 *
 * returns 0 on success, non zero on failure.
 */
int picofly_reset_train_data(void)
{
    gfx_printf("Initialising eMMC...\n");
    emmc_initialize(false);
    sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);

    gfx_printf("Writing training data reset command...\n");
    int res = _picofly_write_cmd(PICOFLY_OPCODE_TRAIN_RESET, 0, 0);

    sdmmc_storage_end(&emmc_storage);

    if (!res)
    {
        gfx_printf("%kTraining data reset command sent!\n%k", 0xFF00FF00, 0xFFCCCCCC);
        gfx_printf("The modchip will retrain on next boot.\n");
    }
    return res;
}

int picofly_backup_sdloader(void)
{
    emmc_initialize(false);
    sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);

    u32 sector_count = (PICOFLY_SDLOADER_SIZE + 511) / 512;
    u8 *buf = malloc(sector_count * 512);
    if (!buf)
    {
        sdmmc_storage_end(&emmc_storage);
        return 1;
    }

    if (!sdmmc_storage_read(&emmc_storage, PICOFLY_SDLOADER_SECTOR, sector_count, buf))
    {
        free(buf);
        sdmmc_storage_end(&emmc_storage);
        return 2;
    }

    sdmmc_storage_end(&emmc_storage);

    sd_mount();
    int res = sd_save_to_file(buf, PICOFLY_SDLOADER_SIZE, "picofly_sdloader.bin");
    sd_end();
    free(buf);
    return res;
}

int picofly_restore_sdloader(void)
{
    sd_mount();
    u32 size = 0;
    u8 *buf = sd_file_read("picofly_sdloader.bin", &size);

    if (!buf)
        return 1;

    if (size != PICOFLY_SDLOADER_SIZE)
    {
        free(buf);
        return 2;
    }

    u32 sector_count = (PICOFLY_SDLOADER_SIZE + 511) / 512;
    u8 *padded = malloc(sector_count * 512);
    if (!padded)
    {
        free(buf);
        return 3;
    }
    memset(padded, 0xFF, sector_count * 512);
    memcpy(padded, buf, size);
    free(buf);

    emmc_initialize(false);
    sdmmc_storage_set_mmc_partition(&emmc_storage, EMMC_BOOT0);

    int res = 0;
    if (!sdmmc_storage_write(&emmc_storage, PICOFLY_SDLOADER_SECTOR, sector_count, padded))
        res = 4;

    free(padded);
    sdmmc_storage_end(&emmc_storage);
    return res;
}