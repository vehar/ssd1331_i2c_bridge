#include "stm32f1xx_hal.h"
#include "serial_cli.h"
#include "cmd_parser.h"
static volatile uint16_t rd = 0, wr = 0;
static volatile char rx[256];
static volatile bool overflow = false;
void cli_uart_init()
{
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_AF_PP; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    USART1->BRR = 625; // 72 MHz / 115200
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}
extern "C" void USART1_IRQHandler()
{
    const uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        char c = USART1->DR;
        uint16_t next = (wr + 1) & 255;
        if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE) || next == rd)
            overflow = true;
        else { rx[wr] = c; __DMB(); wr = next; }
    }
}
int serialAvailable()
{
    // On loss, discard the incomplete command rather than execute corrupted input.
    if (overflow) {
        __disable_irq(); rd = wr; overflow = false; __enable_irq();
        cli_init();
        cli_puts("\r\nERR: UART RX overflow; resend command\r\n> ");
        return 0;
    }
    return (wr - rd) & 255;
}
char serialRead() { char c = rx[rd]; rd = (rd + 1) & 255; return c; }
void serialWrite(char c)
{
    uint32_t start = HAL_GetTick();
    while (!(USART1->SR & USART_SR_TXE))
        if (uint32_t(HAL_GetTick() - start) >= 20) return;
    USART1->DR = uint8_t(c);
}
