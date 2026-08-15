/**
  ******************************************************************************
  * @file           : map_file.h
  * @brief          : File mapping
  * @copyright      : Copyright (c) 2026 stlink-org. All rights reserved.
  * @date           : 2026-07-27
  * SPDX-License-Identifier: BSD-3-Clause
  *
  * This file is licensed under the BSD 3-Clause License.
  * See the LICENSE file in the project root for full license information.
  ******************************************************************************
  */

#ifndef MAP_FILE_H
#define MAP_FILE_H

#ifndef O_BINARY
#define O_BINARY 0
#endif // O_BINARY

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <stlink.h>
#include <win32/mmap.h>

/* Memory mapped file */
typedef struct mapped_file {
  uint8_t *base;
  uint32_t len;
} mapped_file_t;

#define MAPPED_FILE_INITIALIZER                                                \
  { NULL, 0 }

int32_t check_file(stlink_t *, mapped_file_t *, stm32_addr_t);
int32_t map_file(mapped_file_t *, const char *);
void unmap_file(mapped_file_t *);

#endif // MAP_FILE_H
