#include "../cmsis/stm32f0xx.h"

extern int main();
extern void usb_handler() __attribute__((interrupt));
extern void dma_handler() __attribute__((interrupt));

static void startup();
static inline void initialize_data(unsigned int* from, unsigned int* data_start, unsigned int* data_end) __attribute__((always_inline));
static inline void initialize_bss(unsigned int* bss_start, unsigned int* bss_end) __attribute__((always_inline));

static void default_handler() __attribute__((interrupt));
static void    tim7_handler() __attribute__((interrupt));


extern unsigned int __data_start__;//start of .data section in RAM
extern unsigned int __data_end__;//end of .data section in RAM
extern unsigned int __bss_end__;//end of .bss section in RAM
extern unsigned int __text_end__;//end of .text section in ROM

void* vectorTable[48] __attribute__(( section(".vectab,\"a\",%progbits@") )) =
  {
    (void*)0x20003FFC,
    (void*)&startup,
    (void*)&default_handler,
    (void*)&default_handler,
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)&default_handler,
    (void*)0x00000000,//reserved
    (void*)0x00000000,//reserved
    (void*)&default_handler,
    (void*)&default_handler,
    (void*)0x00000000,//IRQ0
    (void*)0x00000000,//IRQ1
    (void*)0x00000000,//IRQ2
    (void*)0x00000000,//IRQ3
    (void*)0x00000000,//IRQ4
    (void*)0x00000000,//IRQ5
    (void*)0x00000000,//IRQ6
    (void*)0x00000000,//IRQ7
    (void*)0x00000000,//IRQ8
    (void*)0x00000000,//IRQ9 
    (void*)&dma_handler,//IRQ10
    (void*)0x00000000,//IRQ11
    (void*)0x00000000,//IRQ12
    (void*)0x00000000,//IRQ13
    (void*)0x00000000,//IRQ14
    (void*)0x00000000,//IRQ15
    (void*)0x00000000,//IRQ16
    (void*)0x00000000,//IRQ17
    (void*)&tim7_handler,//IRQ18
    (void*)0x00000000,//IRQ19
    (void*)0x00000000,//IRQ20
    (void*)0x00000000,//IRQ21
    (void*)0x00000000,//IRQ22
    (void*)0x00000000,//IRQ23
    (void*)0x00000000,//IRQ24
    (void*)0x00000000,//IRQ25
    (void*)0x00000000,//IRQ26
    (void*)0x00000000,//IRQ27
    (void*)0x00000000,//IRQ28
    (void*)0x00000000,//IRQ29
    (void*)0x00000000,//IRQ30
    (void*)&usb_handler//IRQ31
  };


static inline void initialize_data(unsigned int* from, unsigned int* data_start, unsigned int* data_end)
{
  while(data_start < data_end)
    {
      *data_start = *from;
      data_start++;
      from++;
    }
}


static inline void initialize_bss(unsigned int* bss_start, unsigned int* bss_end)
{
  while(bss_start < bss_end)
    {
      *bss_start = 0x00000000U;
      bss_start++;
    }
}


static void startup()
{
  RCC->AHBENR |= (1<<18)|(1<<17)|(1<<0);
  RCC->APB1ENR |= (1<<5)|(1<<4)|(1<<0);
  RCC->APB2ENR |= (1<<12);
  
  GPIOB->MODER |= (1<<22)|(1<<14)|(1<<2);
  GPIOB->BSRR = (1<<11)|(1<<1);  
  GPIOA->MODER |= (1<<15)|(1<<13)|(1<<11)|(1<<6);
  GPIOA->BSRR = (1<<3);
  GPIOA->PUPDR |= (1<<4);
  
  FLASH->ACR = (1<<4)|(1<<0); 
  
  RCC->CR |= (1<<19)|(1<<16);
  while( !(RCC->CR & (1<<17)) );
  RCC->CFGR |= (1<<20)|(1<<16);
  RCC->CR |= (1<<24);
  
  initialize_data(&__text_end__, &__data_start__, &__data_end__);
  initialize_bss(&__data_end__, &__bss_end__);
  
  while( !(RCC->CR & (1<<25)) );
  RCC->CFGR |= (1<<1); 
  while( !((RCC->CFGR & 0x0F) == 0b1010) );
  RCC->CR &= ~(1<<0);
  
  main();
  
  NVIC_SystemReset();
  return;
}


static void default_handler()
{
  NVIC_SystemReset();
  return;
}


static void tim7_handler()
{
  GPIOB->BSRR = (1<<23);
  TIM7->SR = 0;
  
  return;
}
