// SPDX-License-Identifier: Apache-2.0 or CC0-1.0
#include "hal.h"
#include <sys/cdefs.h>

#define SERIAL_BAUD 38400

#include <libopencm3/cm3/dwt.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>

#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/rng.h>
#include <libopencm3/stm32/usart.h>

/* Nucleo boards built on a baseline (non-L4+) STM32L4. They share the console
   peripheral (USART2 via the ST-LINK VCP), the lack of a port G, the 80MHz
   maximum and the baseline flash wait-state table; only the console pins
   differ. This must be tested before STM32L4, which they also define. */
#if defined(NUCLEO_L476RG_BOARD) || defined(NUCLEO_L432KC_BOARD)
#define STM32L4_BASELINE_BOARD
#endif

#if defined(NUCLEO_L476RG_BOARD)
/* USART2 on PA2 (TX) / PA3 (RX), wired to the ST-LINK virtual COM port */
#define SERIAL_GPIO GPIOA
#define SERIAL_GPIO_CLK RCC_GPIOA
#define SERIAL_USART USART2
#define SERIAL_USART_CLK RCC_USART2
#define SERIAL_PINS (GPIO2 | GPIO3)
#define SERIAL_AF GPIO_AF7
#elif defined(NUCLEO_L432KC_BOARD)
/* USART2 via the ST-LINK virtual COM port, but on this board RX is routed to
   PA15 rather than PA3, and the two pins need different alternate functions:
   PA2 is USART2_TX on AF7 while PA15 is USART2_RX on AF3. */
#define SERIAL_GPIO GPIOA
#define SERIAL_GPIO_CLK RCC_GPIOA
#define SERIAL_USART USART2
#define SERIAL_USART_CLK RCC_USART2
#define SERIAL_PIN_TX GPIO2
#define SERIAL_PIN_RX GPIO15
#define SERIAL_PINS (SERIAL_PIN_TX | SERIAL_PIN_RX)
#define SERIAL_AF GPIO_AF7
#define SERIAL_AF_RX GPIO_AF3
#elif defined(STM32L4)
#define SERIAL_GPIO GPIOG
#define SERIAL_GPIO_CLK RCC_GPIOG
#define SERIAL_USART LPUART1
#define SERIAL_USART_CLK RCC_LPUART1
#define SERIAL_PINS (GPIO8 | GPIO7)
#define SERIAL_AF GPIO_AF8
#define NUCLEO_L4R5_BOARD
#elif defined(STM32F4)
/* USART2 on PA2 (TX) / PA3 (RX), connected to an external USB-UART adapter */
#define SERIAL_GPIO GPIOA
#define SERIAL_GPIO_CLK RCC_GPIOA
#define SERIAL_USART USART2
#define SERIAL_USART_CLK RCC_USART2
#define SERIAL_PINS (GPIO2 | GPIO3)
#define SERIAL_AF GPIO_AF7
#else
#error "Unsupported target: define STM32L4 or STM32F4"
#endif

/* Only the L4R5ZI needs this: it writes the 5-bit PLLPDIV field at bits 31:27,
   which exists on L4+ parts but is reserved on the STM32L476. */
#if defined(STM32L4) && !defined(STM32L4_BASELINE_BOARD)
/* Patched function for newer PLL not yet supported by opencm3 */
static void _rcc_set_main_pll(uint32_t source, uint32_t pllm, uint32_t plln, uint32_t pllp,
                              uint32_t pllq, uint32_t pllr) {
    RCC_PLLCFGR = (RCC_PLLCFGR_PLLM(pllm) << RCC_PLLCFGR_PLLM_SHIFT) |
                  (plln << RCC_PLLCFGR_PLLN_SHIFT) | ((pllp & 0x1Fu) << 27u) | /* NEWER PLLP */
                  (source << RCC_PLLCFGR_PLLSRC_SHIFT) | (pllq << RCC_PLLCFGR_PLLQ_SHIFT) |
                  (pllr << RCC_PLLCFGR_PLLR_SHIFT) | RCC_PLLCFGR_PLLREN;
}
#endif

#define _RCC_CAT(A, B) A##_##B
#define RCC_ID(NAME) _RCC_CAT(RCC, NAME)

__attribute__((unused)) static uint32_t _clock_freq;

#ifdef STM32F2
extern uint32_t rcc_apb1_frequency;
extern uint32_t rcc_apb2_frequency;
#endif

#if defined(STM32L4_BASELINE_BOARD)
static void clock_setup(enum clock_mode clock) {
    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_SYSCFG);

    switch (clock) {
    case CLOCK_BENCHMARK:
        /* Benchmark straight from the HSI16 without prescaling. 16MHz is the
           fastest this part runs at zero flash wait states in VCORE range 1
           (1WS is needed above 16MHz), so cycle counts are not skewed by
           flash stalls. */
        pwr_set_vos_scale(PWR_SCALE1);
        rcc_osc_on(RCC_HSI16);
        rcc_wait_for_osc_ready(RCC_HSI16);
        rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
        rcc_set_ppre1(RCC_CFGR_PPRE_NODIV);
        rcc_set_ppre2(RCC_CFGR_PPRE_NODIV);
        flash_dcache_enable();
        flash_icache_enable();
        flash_set_ws(FLASH_ACR_LATENCY_0WS);
        flash_prefetch_enable();
        rcc_set_sysclk_source(RCC_CFGR_SW_HSI16);
        rcc_wait_for_sysclk_status(RCC_HSI16);
        rcc_ahb_frequency = 16000000;
        rcc_apb1_frequency = 16000000;
        rcc_apb2_frequency = 16000000;
        _clock_freq = 16000000;
        break;
    case CLOCK_FAST:
    default:
        /* 80MHz is the maximum for the STM32L476. The stock opencm3 config
           (HSI16 /4 *40 /2, range 1, 4 wait states) is exactly that, and
           rcc_clock_setup_pll also enables RCC_PWR, waits for PLL lock and
           sets the rcc_*_frequency globals. */
        rcc_clock_setup_pll(&rcc_hsi16_configs[RCC_CLOCK_VRANGE1_80MHZ]);
        flash_prefetch_enable();
        _clock_freq = 80000000;
        break;
    }
    /* No RNG here: the STM32L476 has no HSI48, so the L4R5ZI's
       rcc_wait_for_osc_ready(RCC_HSI48) below would spin forever. Nothing in
       this repo reads the RNG, so it is simply left disabled. */
}
#elif defined(STM32L4)
static void clock_setup(enum clock_mode clock) {
    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_SYSCFG);
    pwr_set_vos_scale(PWR_SCALE1);
    /* The L4R5ZI chip also needs the R1MODE bit in PWR_CR5 register set, but
       OpenCM3 doesn't support this yet. But luckily the default value for the bit
       is 1. */
    switch (clock) {
    case CLOCK_BENCHMARK:
        /* Benchmark straight from the HSI16 without prescaling */
        rcc_osc_on(RCC_HSI16);
        rcc_wait_for_osc_ready(RCC_HSI16);
        rcc_ahb_frequency = 20000000;
        rcc_apb1_frequency = 20000000;
        rcc_apb2_frequency = 20000000;
        _clock_freq = 20000000;
        rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
        rcc_set_ppre1(RCC_CFGR_PPRE_NODIV);
        rcc_set_ppre2(RCC_CFGR_PPRE_NODIV);
        rcc_osc_off(RCC_PLL);
        while (rcc_is_osc_ready(RCC_PLL))
            ;
        /* Configure the PLL oscillator (use CUBEMX tool -> scale HSI16 to 20MHz). */
        _rcc_set_main_pll(RCC_PLLCFGR_PLLSRC_HSI16, 1, 10, 2, RCC_PLLCFGR_PLLQ_DIV2,
                          RCC_PLLCFGR_PLLR_DIV8);
        /* Enable PLL oscillator and wait for it to stabilize. */
        rcc_osc_on(RCC_PLL);
        flash_dcache_enable();
        flash_icache_enable();
        flash_set_ws(FLASH_ACR_LATENCY_0WS);
        flash_prefetch_enable();
        rcc_set_sysclk_source(RCC_CFGR_SW_PLL);
        rcc_wait_for_sysclk_status(RCC_PLL);
        break;
    case CLOCK_FAST:
    default:
        rcc_osc_on(RCC_HSI16);
        rcc_wait_for_osc_ready(RCC_HSI16);
        rcc_ahb_frequency = 120000000;
        rcc_apb1_frequency = 120000000;
        rcc_apb2_frequency = 120000000;
        _clock_freq = 120000000;
        rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
        rcc_set_ppre1(RCC_CFGR_PPRE_NODIV);
        rcc_set_ppre2(RCC_CFGR_PPRE_NODIV);
        rcc_osc_off(RCC_PLL);
        while (rcc_is_osc_ready(RCC_PLL))
            ;
        /* Configure the PLL oscillator (use CUBEMX tool -> scale HSI16 to 120MHz). */
        _rcc_set_main_pll(RCC_PLLCFGR_PLLSRC_HSI16, 1, 15, 2, RCC_PLLCFGR_PLLQ_DIV2,
                          RCC_PLLCFGR_PLLR_DIV2);
        /* Enable PLL oscillator and wait for it to stabilize. */
        rcc_osc_on(RCC_PLL);
        rcc_wait_for_osc_ready(RCC_PLL);
        flash_dcache_enable();
        flash_icache_enable();
        flash_set_ws(0x05);
        flash_prefetch_enable();
        rcc_set_sysclk_source(RCC_CFGR_SW_PLL);
        rcc_wait_for_sysclk_status(RCC_PLL);
        break;
    }
    rcc_osc_on(RCC_HSI48); /* HSI48 must always be on for RNG */
    rcc_wait_for_osc_ready(RCC_HSI48);
    rcc_periph_clock_enable(RCC_RNG);
    rcc_set_clock48_source(RCC_CCIPR_CLK48SEL_HSI48);
    rng_enable();
}
#elif defined(STM32F4)
/* Benchmark clock: PLL from HSI16 to 24MHz, low enough for zero flash wait
   states (up to 30MHz on the F407) so cycle counts are not skewed by flash. */
static const struct rcc_clock_scale benchmark_clock = {
    .pllm = 16,  /* VCOin = HSI16 / 16 = 1MHz */
    .plln = 192, /* VCOout = 192MHz */
    .pllp = 8,   /* SYSCLK = 24MHz */
    .pllq = 4,   /* PLL48CLK = 48MHz (RNG) */
    .pllr = 0,
    .pll_source = RCC_CFGR_PLLSRC_HSI_CLK,
    .flash_config = FLASH_ACR_DCEN | FLASH_ACR_ICEN | FLASH_ACR_LATENCY_0WS,
    .hpre = RCC_CFGR_HPRE_NODIV,
    .ppre1 = RCC_CFGR_PPRE_NODIV,
    .ppre2 = RCC_CFGR_PPRE_NODIV,
    .voltage_scale = PWR_SCALE1,
    .ahb_frequency = 24000000,
    .apb1_frequency = 24000000,
    .apb2_frequency = 24000000,
};

static void clock_setup(enum clock_mode clock) {
    /* Both modes run from HSI16 so no particular crystal is required */
    switch (clock) {
    case CLOCK_BENCHMARK:
        rcc_clock_setup_pll(&benchmark_clock);
        _clock_freq = 24000000;
        break;
    case CLOCK_FAST:
    default:
        rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_168MHZ]);
        flash_prefetch_enable();
        _clock_freq = 168000000;
        break;
    }
    rcc_periph_clock_enable(RCC_RNG);
    rng_enable();
}
#endif

static void usart_setup(void) {
    rcc_periph_clock_enable(SERIAL_GPIO_CLK);
    rcc_periph_clock_enable(SERIAL_USART_CLK);

#if defined(STM32L4) && !defined(STM32L4_BASELINE_BOARD)
    /* Port G pins PG[15:2] are powered from VDDIO2 and stay isolated until
       IOSV is set. Only applies to packages that have port G; CR2 bit 9 is
       reserved on the 64-pin L476RG, whose console is on port A. */
    PWR_CR2 |= PWR_CR2_IOSV;
#endif
    gpio_set_output_options(SERIAL_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, SERIAL_PINS);
#if defined(SERIAL_AF_RX)
    /* TX and RX sit on pins with different alternate functions, so they have
       to be programmed separately. */
    gpio_set_af(SERIAL_GPIO, SERIAL_AF, SERIAL_PIN_TX);
    gpio_set_af(SERIAL_GPIO, SERIAL_AF_RX, SERIAL_PIN_RX);
#else
    gpio_set_af(SERIAL_GPIO, SERIAL_AF, SERIAL_PINS);
#endif
    gpio_mode_setup(SERIAL_GPIO, GPIO_MODE_AF, GPIO_PUPD_NONE, SERIAL_PINS);
    usart_set_baudrate(SERIAL_USART, SERIAL_BAUD);
    usart_set_databits(SERIAL_USART, 8);
    usart_set_stopbits(SERIAL_USART, USART_STOPBITS_1);
    usart_set_mode(SERIAL_USART, USART_MODE_TX_RX);
    usart_set_parity(SERIAL_USART, USART_PARITY_NONE);
    usart_set_flow_control(SERIAL_USART, USART_FLOWCONTROL_NONE);
    usart_disable_rx_interrupt(SERIAL_USART);
    usart_disable_tx_interrupt(SERIAL_USART);
    usart_enable(SERIAL_USART);
}

static void systick_setup(void) {
    /* Systick is always the same on libopencm3 */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(0xFFFFFFu);
    systick_interrupt_enable();
    systick_counter_enable();
}
static volatile unsigned long long overflowcnt = 0;
void hal_setup(const enum clock_mode clock) {
    clock_setup(clock);
    usart_setup();
    systick_setup();

    // wait for the first systick overflow
    // improves reliability of the benchmarking scripts since it makes it much
    // less likely that the host will miss the start of the output
    unsigned long long old = overflowcnt;
    while (old == overflowcnt)
        ;
}

void hal_send_str(const char *in) {
    const char *cur = in;
    while (*cur) {
        usart_send_blocking(SERIAL_USART, *cur);
        cur += 1;
    }
    usart_send_blocking(SERIAL_USART, '\n');
}

/**
 * @brief Receive a string from the host via UART.
 *
 * Blocks until a newline ('\n' or '\r') or buffer full.
 *
 * @param[out] buf    Destination buffer
 * @param[in]  len    Buffer length (including space for '\0')
 */
uint32_t hal_recv_str(char *buf, uint32_t len) {
    uint32_t i = 0;

    if (len == 0)
        return 0;

    while (i < (len - 1)) {
        char c = usart_recv_blocking(SERIAL_USART);
        // Stop at newline or carriage return
        if (c == '\n' || c == '\r')
            break;

        buf[i++] = c;
    }

    buf[i] = '\0'; // Null-terminate
    return len;
}

void sys_tick_handler(void) { ++overflowcnt; }

uint64_t hal_get_time() {
    while (true) {
        unsigned long long before = overflowcnt;
        unsigned long long result = (before + 1) * 16777216llu - systick_get_value();
        if (overflowcnt == before) {
            return result;
        }
    }
}

/* End of BSS is where the heap starts (defined in the linker script) */
extern char end;
static char *heap_end = &end;

size_t hal_get_stack_size(void) {
    register char *cur_stack;
    asm volatile("mov %0, sp" : "=r"(cur_stack));
    return cur_stack - heap_end;
}

const uint32_t stackpattern = 0xDEADBEEFlu;

static void *last_sp = NULL;

void hal_spraystack(void) {

    char *_heap_end = heap_end;
    asm volatile("mov %0, sp\n"
                 ".L%=:\n\t"
                 "str %2, [%1], #4\n\t"
                 "cmp %1, %0\n\t"
                 "blt .L%=\n\t"
                 : "+r"(last_sp), "+r"(_heap_end)
                 : "r"(stackpattern)
                 : "cc", "memory");
}

size_t hal_checkstack(void) {
    size_t result = 0;
    asm volatile("sub %0, %1, %2\n"
                 ".L%=:\n\t"
                 "ldr ip, [%2], #4\n\t"
                 "cmp ip, %3\n\t"
                 "ite eq\n\t"
                 "subeq %0, #4\n\t"
                 "bne .LE%=\n\t"
                 "cmp %2, %1\n\t"
                 "blt .L%=\n\t"
                 ".LE%=:\n"
                 : "+r"(result)
                 : "r"(last_sp), "r"(heap_end), "r"(stackpattern)
                 : "ip", "cc");
    return result;
}

/* Implement some system calls to shut up the linker warnings */

#include <errno.h>
#include <sys/stat.h>

#undef errno
extern int errno;

/* Syscall wrapper prototypes */
void *__wrap__sbrk(int incr);
int __wrap__open(char *file, int flags, int mode);
int __wrap__close(int fd);
int __wrap__fstat(int fd, struct stat *buf);
int __wrap__getpid(void);
int __wrap__isatty(int file);
int __wrap__kill(int pid, int sig);
int __wrap__lseek(int fd, int ptr, int dir);
int __wrap__read(int fd, char *ptr, int len);
int __wrap__write(int fd, const char *ptr, int len);

int __wrap__open(char *file, int flags, int mode) {
    (void)file;
    (void)flags;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int __wrap__close(int fd) {
    errno = ENOSYS;
    (void)fd;
    return -1;
}

int __wrap__fstat(int fd, struct stat *buf) {
    (void)fd;
    (void)buf;
    errno = ENOSYS;
    return -1;
}

int __wrap__getpid(void) {
    errno = ENOSYS;
    return -1;
}

int __wrap__isatty(int file) {
    (void)file;
    errno = ENOSYS;
    return 0;
}

int __wrap__kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}

int __wrap__lseek(int fd, int ptr, int dir) {
    (void)fd;
    (void)ptr;
    (void)dir;
    errno = ENOSYS;
    return -1;
}

int __wrap__read(int fd, char *ptr, int len) {
    (void)fd;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int __wrap__write(int fd, const char *ptr, int len) {
    (void)fd;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

void *__wrap__sbrk(int incr) {
    char *prev_heap_end;

    prev_heap_end = heap_end;
    heap_end += incr;

    return (void *)prev_heap_end;
}