#include "stm32f1xx_hal.h"
#include "config.h"
#include "Bridge.h"
#include <string.h>
static SPI_HandleTypeDef spi;
static void die() { __disable_irq(); while(true) {} }
extern "C" void SysTick_Handler() {HAL_IncTick();}
static void clocks() {
    RCC_OscInitTypeDef o{};o.OscillatorType=RCC_OSCILLATORTYPE_HSE;
    o.HSEState=RCC_HSE_ON;o.HSEPredivValue=RCC_HSE_PREDIV_DIV1;
    o.PLL.PLLState=RCC_PLL_ON;o.PLL.PLLSource=RCC_PLLSOURCE_HSE;o.PLL.PLLMUL=RCC_PLL_MUL9;
    if(HAL_RCC_OscConfig(&o)!=HAL_OK) die();
    RCC_ClkInitTypeDef c{};c.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;c.AHBCLKDivider=RCC_SYSCLK_DIV1;
    c.APB1CLKDivider=RCC_HCLK_DIV2;c.APB2CLKDivider=RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&c,FLASH_LATENCY_2)!=HAL_OK) die();
}
class SSD1331 final:public oled::Panel {
    void tx(bool dataMode,const uint8_t* b,size_t n) {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,dataMode?GPIO_PIN_SET:GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_RESET);
        if(HAL_SPI_Transmit(&spi,const_cast<uint8_t*>(b),uint16_t(n),100)!=HAL_OK) die();
        HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);
    }
public:
    void command(const uint8_t* b,size_t n) override {tx(false,b,n);}
    void data(const uint8_t* b,size_t n) override {tx(true,b,n);}
    void wait(uint32_t ms) override {HAL_Delay(ms);}
    void init() override {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_1,GPIO_PIN_RESET);HAL_Delay(10);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_1,GPIO_PIN_SET);HAL_Delay(10);
        const uint8_t setup[]={0xFD,0x12,0xAE,0x2E,0xA0,BRIDGE_REMAP,0xA1,0,0xA2,0,
            0xA4,0xA8,63,0xAD,0x8E,0xB0,0x0B,0xB1,0x31,0xB3,0xF0,
            0x8A,0x64,0x8B,0x78,0x8C,0x64,0xBB,0x3A,0xBE,0x3E,
            0x87,0x06,0x81,0x91,0x82,0x50,0x83,0x7D,0xB9,0x25,0,0,95,63};
        command(setup,sizeof setup);HAL_Delay(3);
        uint8_t on=0xAF;command(&on,1);HAL_Delay(100);
    }
};
static SSD1331 panel;
static oled::Bridge bridge(panel,BRIDGE_VIRTUAL_WIDTH,BRIDGE_VIEWPORT_X);
static_assert((BRIDGE_QUEUE_SIZE&(BRIDGE_QUEUE_SIZE-1))==0,"queue must be power of two");
static_assert(BRIDGE_QUEUE_SIZE<=32768,"queue index must fit uint16_t");
static volatile uint16_t events[BRIDGE_QUEUE_SIZE];
static volatile uint16_t head=0,tail=0;
static volatile bool queueFailed=false,busFailed=false;
static volatile bool processing=false;
static bool addressedWrite=false,transmitting=false;
static uint8_t publishedStatus[16]{},readStatus[16]{};
static uint8_t txIndex=0;
static void push(uint16_t v) {
    if(queueFailed) return;
    uint16_t next=(head+1)&(BRIDGE_QUEUE_SIZE-1);
    if(next==tail) {queueFailed=true;return;}
    events[head]=v;__DMB();head=next;
}
static void endWrite() {if(addressedWrite){push(0x101);addressedWrite=false;}}
extern "C" void I2C1_EV_IRQHandler() {
    uint32_t sr=I2C1->SR1;
    // Drain old RX data before an address/STOP boundary, including BTF's two bytes.
    if(!transmitting) {
        while(I2C1->SR1&I2C_SR1_RXNE) {uint8_t b=I2C1->DR;if(addressedWrite)push(b);}
    }
    if(sr&I2C_SR1_ADDR) {
        uint32_t s2=I2C1->SR2; // SR1 then SR2 clears ADDR; stretching ends here
        endWrite();transmitting=(s2&I2C_SR2_TRA)!=0;
        I2C1->CR2|=I2C_CR2_ITBUFEN;
        if(transmitting) {
            memcpy(readStatus,publishedStatus,16);
            uint16_t depth=(head-tail)&(BRIDGE_QUEUE_SIZE-1);
            readStatus[8]=depth;readStatus[9]=depth>>8;
            if(processing)readStatus[4]|=8;
            if(queueFailed) readStatus[3]=oled::OVERFLOW;
            if(busFailed) readStatus[3]=oled::BUS;
            txIndex=0;I2C1->DR=readStatus[txIndex++];
        } else {addressedWrite=true;push(0x100);}
    }
    if(sr&I2C_SR1_STOPF) {
        (void)I2C1->SR1;I2C1->CR1|=I2C_CR1_PE; // clear STOPF
        endWrite();transmitting=false;
    }
    if(transmitting && (I2C1->SR1&I2C_SR1_TXE))
        I2C1->DR=txIndex<16?readStatus[txIndex++]:0xFF;
}
extern "C" void I2C1_ER_IRQHandler() {
    uint32_t s=I2C1->SR1;
    if(s&I2C_SR1_AF) {I2C1->SR1&=~I2C_SR1_AF;transmitting=false;I2C1->CR2&=~I2C_CR2_ITBUFEN;} // normal read NACK
    if(s&(I2C_SR1_BERR|I2C_SR1_ARLO|I2C_SR1_OVR|I2C_SR1_TIMEOUT)) {
        I2C1->SR1&=~(I2C_SR1_BERR|I2C_SR1_ARLO|I2C_SR1_OVR|I2C_SR1_TIMEOUT);
        busFailed=true;addressedWrite=false;transmitting=false;
    }
}
static void peripherals() {
    __HAL_RCC_AFIO_CLK_ENABLE();__HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();__HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef g{};g.Speed=GPIO_SPEED_FREQ_HIGH;
    g.Pin=GPIO_PIN_4;g.Mode=GPIO_MODE_OUTPUT_PP;HAL_GPIO_Init(GPIOA,&g);
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);
    g.Pin=GPIO_PIN_0|GPIO_PIN_1;HAL_GPIO_Init(GPIOB,&g);
    g.Pin=GPIO_PIN_5|GPIO_PIN_7;g.Mode=GPIO_MODE_AF_PP;HAL_GPIO_Init(GPIOA,&g);
    g.Pin=GPIO_PIN_6|GPIO_PIN_7;g.Mode=GPIO_MODE_AF_OD;HAL_GPIO_Init(GPIOB,&g);
    spi.Instance=SPI1;spi.Init.Mode=SPI_MODE_MASTER;spi.Init.Direction=SPI_DIRECTION_1LINE;
    spi.Init.DataSize=SPI_DATASIZE_8BIT;spi.Init.CLKPolarity=SPI_POLARITY_LOW;
    spi.Init.CLKPhase=SPI_PHASE_1EDGE;spi.Init.NSS=SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_16; // 4.5 MHz below SSD1331 limit
    spi.Init.FirstBit=SPI_FIRSTBIT_MSB;spi.Init.TIMode=SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;spi.Init.CRCPolynomial=7;
    if(HAL_SPI_Init(&spi)!=HAL_OK)die();
}
static void listen() {
    // Own F1 interrupt driver, no HAL I2C callbacks and no fixed-length buffers.
    I2C1->CR1=I2C_CR1_SWRST;I2C1->CR1=0;
    I2C1->CR2=36; // APB1 MHz; master sets the actual bus frequency
    I2C1->OAR1=(1U<<14)|(BRIDGE_I2C_ADDRESS<<1);
    I2C1->OAR2=0;I2C1->CCR=180;I2C1->TRISE=37;
    I2C1->CR1=I2C_CR1_PE|I2C_CR1_ACK; // clock stretching enabled
    I2C1->CR2|=I2C_CR2_ITEVTEN|I2C_CR2_ITBUFEN|I2C_CR2_ITERREN;
    HAL_NVIC_SetPriority(I2C1_EV_IRQn,0,0);HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn,0,0);HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}
int main() {
    HAL_Init();clocks();peripherals();bridge.reset();bridge.render();
    bridge.status(publishedStatus);listen();
    while(true) {
        if(queueFailed || busFailed) {
            processing=true;
            __disable_irq();bool ov=queueFailed;tail=head;queueFailed=busFailed=false;
            addressedWrite=false;__enable_irq();bridge.abort();bridge.fault(ov?oled::OVERFLOW:oled::BUS);
        }
        // Bounded drain so continuous traffic cannot starve rendering/status.
        for(unsigned i=0;i<2048 && tail!=head;++i) {
            processing=true;
            uint16_t e=events[tail];__DMB();tail=(tail+1)&(BRIDGE_QUEUE_SIZE-1);
            if(e==0x100)bridge.start();else if(e==0x101)bridge.stop();else bridge.byte(e);
        }
        // Defer automatic redraw to the end of a burst. Extended commands run in order.
        if(tail==head && bridge.workDue(HAL_GetTick())) {processing=true;bridge.tick(HAL_GetTick());}
        uint8_t s[16];bridge.status(s);
        __disable_irq();memcpy(publishedStatus,s,16);processing=false;__enable_irq();
    }
}
