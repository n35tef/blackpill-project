/**
 * @file    bsp_usart.c
 * @brief   USART1, USART2 and USART6 in asynchronous 8N1 mode.
 */

#include "bsp_usart.h"

#include "bsp_clock.h"
#include "bsp_gpio.h"

#if BSP_USE_USART1 || BSP_USE_USART2 || BSP_USE_USART6

/** @brief Peripheral clock feeding a USART; USART2 hangs off APB1. */
static uint32_t usart_clock_hz(const usart_regs_t* usart)
{
    return (usart == USART2) ? BSP_PCLK1_HZ : BSP_PCLK2_HZ;
}

/**
 * @brief Compute BRR for 16x oversampling.
 *
 * BRR holds the divider in 12.4 fixed point, so rounding the scaled value is
 * enough - no separate mantissa/fraction assembly is needed at OVER8 = 0.
 */
static uint32_t baud_to_brr(uint32_t pclk_hz, uint32_t baud)
{
    return (pclk_hz + (baud / 2U)) / baud;
}

static void usart_configure(usart_regs_t* usart, uint32_t baud)
{
    usart->CR1 = 0U;
    usart->CR2 = USART_STOP_1 << USART_CR2_STOP_POS;
    usart->CR3 = 0U;
    usart->BRR = baud_to_brr(usart_clock_hz(usart), baud);
    usart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void bsp_usart_init(void)
{
#if BSP_USE_USART1
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    bsp_gpio_config_alternate(BSP_USART1_TX_PORT, BSP_USART1_TX_PIN, BSP_USART1_TX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_USART1_RX_PORT, BSP_USART1_RX_PIN, BSP_USART1_RX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    usart_configure(USART1, BSP_USART1_BAUD);
#endif

#if BSP_USE_USART2
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_USART2_TX_PORT, BSP_USART2_TX_PIN, BSP_USART2_TX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_USART2_RX_PORT, BSP_USART2_RX_PIN, BSP_USART2_RX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    usart_configure(USART2, BSP_USART2_BAUD);
#endif

#if BSP_USE_USART6
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    (void)RCC->APB2ENR;
    bsp_gpio_config_alternate(BSP_USART6_TX_PORT, BSP_USART6_TX_PIN, BSP_USART6_TX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_USART6_RX_PORT, BSP_USART6_RX_PIN, BSP_USART6_RX_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    usart_configure(USART6, BSP_USART6_BAUD);
#endif
}

void bsp_usart_write_byte(usart_regs_t* usart, uint8_t byte)
{
    while ((usart->SR & USART_SR_TXE) == 0U)
    {
    }
    usart->DR = byte;
}

void bsp_usart_write(usart_regs_t* usart, const uint8_t* data, size_t length)
{
    for (size_t i = 0U; i < length; i++)
    {
        bsp_usart_write_byte(usart, data[i]);
    }
}

void bsp_usart_write_string(usart_regs_t* usart, const char* text)
{
    while (*text != '\0')
    {
        bsp_usart_write_byte(usart, (uint8_t)*text++);
    }
}

uint8_t bsp_usart_read_byte(usart_regs_t* usart)
{
    while ((usart->SR & USART_SR_RXNE) == 0U)
    {
    }
    return (uint8_t)usart->DR;
}

bool bsp_usart_read_byte_nonblocking(usart_regs_t* usart, uint8_t* byte)
{
    if ((usart->SR & USART_SR_RXNE) == 0U)
    {
        return false;
    }
    *byte = (uint8_t)usart->DR;
    return true;
}

void bsp_usart_flush(usart_regs_t* usart)
{
    while ((usart->SR & USART_SR_TC) == 0U)
    {
    }
}

void bsp_usart_set_baud(usart_regs_t* usart, uint32_t baud)
{
    const uint32_t cr1 = usart->CR1;

    bsp_usart_flush(usart);
    usart->CR1 = cr1 & ~USART_CR1_UE;
    usart->BRR = baud_to_brr(usart_clock_hz(usart), baud);
    usart->CR1 = cr1;
}

/* ------------------------------------------------------------------------ */
/* printf() retargeting                                                      */
/* ------------------------------------------------------------------------ */
#if BSP_STDOUT_USART != 0

#if BSP_STDOUT_USART == 1
#if !BSP_USE_USART1
#error "BSP_STDOUT_USART is 1 but BSP_USE_USART1 is 0"
#endif
#define STDOUT_PORT USART1
#elif BSP_STDOUT_USART == 2
#if !BSP_USE_USART2
#error "BSP_STDOUT_USART is 2 but BSP_USE_USART2 is 0"
#endif
#define STDOUT_PORT USART2
#elif BSP_STDOUT_USART == 6
#if !BSP_USE_USART6
#error "BSP_STDOUT_USART is 6 but BSP_USE_USART6 is 0"
#endif
#define STDOUT_PORT USART6
#else
#error "BSP_STDOUT_USART must be 0, 1, 2 or 6"
#endif

/* Overrides the weak stubs in bsp_syscalls.c. */
int bsp_putchar(int ch)
{
    if (ch == '\n')
    {
        bsp_usart_write_byte(STDOUT_PORT, (uint8_t)'\r');
    }
    bsp_usart_write_byte(STDOUT_PORT, (uint8_t)ch);
    return ch;
}

int bsp_getchar(void)
{
    return (int)bsp_usart_read_byte(STDOUT_PORT);
}

#endif /* BSP_STDOUT_USART */

#endif /* any USART */
