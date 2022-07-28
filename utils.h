// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _UTILS_H_
#define _UTILS_H_

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>

/*- Prototypes --------------------------------------------------------------*/
void sha256(uint8_t *data, int size, uint8_t *hash);
uint32_t crc32(uint8_t *data, int size);

#endif // _UTILS_H_

