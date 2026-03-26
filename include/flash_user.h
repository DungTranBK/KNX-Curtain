/*
 * flash_user.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTranBK
 */

#ifndef FLASH_USER_H_
#define FLASH_USER_H_

#include "utilities.h"
/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

#define FLASH_ADR_LOCK_SCHEDULE 0x0000  // Placeholder address

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/

/* Stubs for flash operations */
static inline void flash_user_store(int* flash_idx, u32 addr, u32 size,
                                    u32 block_size, u8* data) {
  // Not implemented
}

static inline void flash_user_restore(int flash_idx, u32 addr, u32 block_size,
                                      u8* data) {
  // Not implemented
}

static inline void flash_user_get_flash_index(int* flash_idx, u32 addr,
                                              u32 size, u32 block_size,
                                              u8* data) {
  *flash_idx = 0;
}

#endif /* FLASH_USER_H_ */
