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

#ifndef _PICOFLY_H_
#define _PICOFLY_H_

#define PICOFLY_SDLOADER_SECTOR     0x1F80
#define PICOFLY_SDLOADER_SIZE       65536

#include <utils/types.h>

int picofly_backup_sdloader(void);
int picofly_restore_sdloader(void);

/*
 * RP2040 communicates via eMMC BOOT0.
 * commands are written as a 64-element u32 array to the second sector
 * of BOOT0 (i.e. at byte offset 512, sector index 1).
 *
 * command layout (u32[64]):
 *   [0] = command magic / opcode
 *   [1] = arg0
 *   [2] = arg1 (firmware update: file size in 512-byte pages)
 *   [3..63] = 0x00000000
 *
 * known commands:
 *   training data reset:  { 0x515205C5, 0, ... }
 *   firmware update:      { 0x6DB92148, <start_block>, <size_in_blocks>, 0, ... }
 *   firmware rollback:    { 0x6DB92148, 0xffffffff, 0xffffffff, 0, ... }
 *
 * for firmware update, the firmware binary (fw_header format) is first written
 * to BOOT0 starting at PICOFLY_FW_START_SECTOR.  the command's arg0 (start_block)
 * must equal PICOFLY_FW_START_SECTOR so the chip reads the firmware from the
 * right place.
 *
 * note: 0x9cabe959 is the signature picofly WRITES to BOOT0 block 0x1FFF as a
 * descriptor after successful operations.  it is NOT a firmware file header.
 *
 * firmware binary format (fw_header):
 *   uint32_t size;
 *   uint32_t crc;
 *   uint8_t  data[];
 */

/* command sector location in BOOT0 */
#define PICOFLY_CMD_SECTOR        1       /* second sector (0-indexed) of BOOT0   */
#define PICOFLY_CMD_WORDS         64      /* command array size in u32's           */

/* command opcodes */
#define PICOFLY_OPCODE_TRAIN_RESET  0x515205C5U
#define PICOFLY_OPCODE_FW_CMD       0x6DB92148U

/* firmware update / rollback arg0 values */
#define PICOFLY_FW_START_SECTOR     0x1f80           /* BOOT0 sector where fw binary is written */
#define PICOFLY_ARG_FW_UPDATE       PICOFLY_FW_START_SECTOR  /* start_block for update cmd */
#define PICOFLY_ARG_FW_ROLLBACK     0xFFFFFFFFU

/* BOOT0 descriptor block written BY picofly after operations (not a firmware header) */
#define PICOFLY_DESCRIPTOR_SIGNATURE 0x9cabe959U

/* maximum sensible firmware size (512 KB) */
#define PICOFLY_FW_MAX_SIZE         (512 * 1024)

/* sector size */
#define PICOFLY_SECTOR_SZ           512

int picofly_update_fw(void);
int picofly_rollback_fw(void);
int picofly_reset_train_data(void);

#endif
