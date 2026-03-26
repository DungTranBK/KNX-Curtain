/*
 * Copyright (c) 2019
 * Lumi, JSC.
 * All Rights Reserved
 *
 *
 * Description:
 *
 * Author: DungTran
 *
 * Last Changed By:  $Author: DungTran $
 * Revision:         $Revision: 1.0 $
 * Last Changed:     $Date: 1/7/19 $
 */
#include "fifo.h"

/** @brief <function's name>
 * <description>
 */
static void copyVoidPointerItem(void* _dst, void* _src, uint8_t size) {
  uint8_t* src = (uint8_t*)_src;
  uint8_t* dst = (uint8_t*)_dst;

  while (size > 0) {
    *dst = *src;

    src += 1;
    dst += 1;

    size -= 1;
  }
}

/** @brief <function's name>
 * <description>
 */
static uint16_t FifoNext(Fifo_t* fifo, uint16_t index) {
  if (fifo != NULL) return (index + 1) % fifo->length;
  return 0xFFFF;
}

/** @brief <function's name>
 * <description>
 */
void FifoInit(Fifo_t* fifo, void* buffer, uint8_t itemSize, uint16_t length) {
  if (fifo != NULL) {
    fifo->head = 0;
    fifo->tail = 0;
    fifo->buffer = buffer;
    fifo->length = length;
    fifo->itemSize = itemSize;
  }
}

/** @brief
 *
 */
bool FifoPush(Fifo_t* fifo, void* data) {
  uint8_t* ptr = (uint8_t*)fifo->buffer;

  if ((fifo != NULL) && (data != NULL)) {
    ptr += fifo->tail * fifo->itemSize;
    copyVoidPointerItem(ptr, data, fifo->itemSize);
    fifo->tail = FifoNext(fifo, fifo->tail);
    return TRUE;
  }
  return FALSE;
}

/** @brief <function's name>
 *
 * <description>
 *
 */
bool FifoPop(Fifo_t* fifo, void* data) {
  uint8_t* ptr = (uint8_t*)fifo->buffer;

  if ((fifo != NULL) && !FifoIsEmpty(fifo)) {
    ptr += fifo->head * fifo->itemSize;
    copyVoidPointerItem(data, ptr, fifo->itemSize);
    fifo->head = FifoNext(fifo, fifo->head);
    return TRUE;
  }
  return FALSE;
}

/** @brief <function's name>
 *
 * Don't need to delete buffer, because it just is a pointer point to sized
 * array
 *
 */
void FifoFlush(Fifo_t* fifo) {
  if (fifo != NULL) {
    fifo->head = 0;
    fifo->tail = 0;
  }
}

/** @brief <function's name>
 *
 * <description>
 *
 */
bool FifoIsEmpty(Fifo_t* fifo) {
  if (fifo != NULL) return (fifo->head == fifo->tail);
  return TRUE;
}

/** @brief <function's name>
 *
 * <description>
 *
 */
bool FifoIsFull(Fifo_t* fifo) {
  if (fifo != NULL) return (FifoNext(fifo, fifo->tail) == fifo->head);
  return TRUE;
}
