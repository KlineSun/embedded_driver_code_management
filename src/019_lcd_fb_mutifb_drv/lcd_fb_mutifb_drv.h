#ifndef LCD_FV_MUTIFB_DRV_H
#define LCD_FV_MUTIFB_DRV_H

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define MIN(x, y) x < y ? x : y


#define MAX_LCD_NAME_LEN (16)
#define ltdc_set_bit(addr, bit)    writel(readl(addr) | BIT(bit), addr)
#define ltdc_clear_bit(addr, bit)  writel(readl(addr) & ~BIT(bit), addr)
#define ARGB8888_COMBINE(a, r, g, b) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

typedef struct {
  volatile unsigned int LTDC_IDR;
  volatile unsigned int LTDC_LCR;
  volatile unsigned int LTDC_SSCR;
  volatile unsigned int LTDC_BPCR;
  volatile unsigned int LTDC_AWCR;
  volatile unsigned int LTDC_TWCR;
  volatile unsigned int LTDC_GCR;
  volatile unsigned int LTDC_GC1R;
  volatile unsigned int LTDC_GC2R;
  volatile unsigned int LTDC_SRCR;
	  unsigned char RESERVED_0[4];
  volatile unsigned int LTDC_BCCR;
	  unsigned char RESERVED_1[4];
  volatile unsigned int LTDC_IER;
  volatile unsigned int LTDC_ISR;
  volatile unsigned int LTDC_ICR;
  volatile unsigned int LTDC_LIPCR;
  volatile unsigned int LTDC_CPSR;
  volatile unsigned int LTDC_CDSR;
	  unsigned char RESERVED_2[56];
  volatile unsigned int LTDC_L1CR;
  volatile unsigned int LTDC_L1WHPCR;
  volatile unsigned int LTDC_L1WVPCR;
  volatile unsigned int LTDC_L1CKCR;
  volatile unsigned int LTDC_L1PFCR;
  volatile unsigned int LTDC_L1CACR;
  volatile unsigned int LTDC_L1DCCR;
  volatile unsigned int LTDC_L1BFCR;
 	  unsigned char RESERVED_3[8]; 
  volatile unsigned int LTDC_L1CFBAR;
  volatile unsigned int LTDC_L1CFBLR;
  volatile unsigned int LTDC_L1CFBLNR;
      unsigned char RESERVED_4[12];  
  volatile unsigned int LTDC_L1CLUTWR;
	  unsigned char RESERVED_5[60];
  volatile unsigned int LTDC_L2CR;
  volatile unsigned int LTDC_L2WHPCR;
  volatile unsigned int LTDC_L2WVPCR;
  volatile unsigned int LTDC_L2CKCR;
  volatile unsigned int LTDC_L2PFCR;
  volatile unsigned int LTDC_L2CACR;
  volatile unsigned int LTDC_L2DCCR;
  volatile unsigned int LTDC_L2BFCR;
	  unsigned char RESERVED_6[8];
  volatile unsigned int LTDC_L2CFBAR;
  volatile unsigned int LTDC_L2CFBLR;
  volatile  unsigned int LTDC_L2CFBLNR;
	  unsigned char RESERVED_7[12]; 
  volatile unsigned int LTDC_L2CLUTWR;
} stm32_ltdc_regs;

struct stm32_ltdc_info {
    // char name[MAX_LCD_NAME_LEN];
    struct device *dev;
    struct gpio_desc  *bl_gpio;
    struct clk *px_clk;
    struct display_timings *dtms;
    stm32_ltdc_regs *regs_vaddr;
};

#endif // LCD_FV_MUTIFB_DRV_H