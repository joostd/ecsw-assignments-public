// SPDX-License-Identifier: Apache-2.0 or CC0-1.0
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdlib.h>

/* Name of the target board, e.g. for startup banners.
   NUCLEO_L476RG_BOARD is checked before STM32L4, which it also defines. */
#if defined(NUCLEO_L476RG_BOARD)
#define HAL_BOARD_NAME "Nucleo-L476RG"
#elif defined(STM32L4)
#define HAL_BOARD_NAME "Nucleo-L4R5ZI"
#elif defined(STM32F4)
#define HAL_BOARD_NAME "STM32F4DISCOVERY"
#elif defined(QEMU_PLATFORM)
#define HAL_BOARD_NAME "QEMU mps2-an386"
#else
#error "Unknown platform: no HAL_BOARD_NAME defined"
#endif

enum clock_mode { CLOCK_FAST, CLOCK_BENCHMARK };

void hal_setup(const enum clock_mode clock);
void hal_send_str(const char *in);
uint32_t hal_recv_str(char *buf, uint32_t len);
uint64_t hal_get_time(void);
size_t hal_get_stack_size(void);
void hal_spraystack(void);
size_t hal_checkstack(void);

#endif