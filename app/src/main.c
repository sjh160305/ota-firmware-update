/******************************************************************************
 * @file     main.c
 * @brief    Application for TD1601 - GPIO key triggered LED toggle
 * @version  V3.0 (支持A/B双分区启动确认)
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <drv/gpio.h>
#include <drv/pin.h>
#include <drv/spiflash.h>
#include "board_config.h"
#include "board_init.h"
#include "boot_ctrl.h"

extern csi_spiflash_t g_spiflash;

static csi_gpio_t led_gpio;
static csi_gpio_t key_gpio;
static volatile uint8_t key_pressed = 0;
static uint8_t led_state = 0;

static void key_irq_handler(csi_gpio_t *gpio, uint32_t pin_mask, void *arg)
{
    if (pin_mask & PIN17) {
        key_pressed = 1;
    }
}

static void key_init(void)
{
    csi_pin_set_mux(PORTB, PIN17, PIN_FUNC_GPIO);
    csi_gpio_init(&key_gpio, PORTB);
    csi_gpio_dir(&key_gpio, PIN17, GPIO_DIRECTION_INPUT);
    csi_gpio_mode(&key_gpio, PIN17, GPIO_MODE_PULLUP);
    csi_gpio_attach_callback(&key_gpio, key_irq_handler, NULL);
    csi_gpio_irq_mode(&key_gpio, PIN17, GPIO_IRQ_MODE_FALLING_EDGE);
    csi_gpio_irq_enable(&key_gpio, PIN17, true);
}

static void led_init(void)
{
    csi_pin_set_mux(PORTA, PIN24|PIN25, PIN_FUNC_GPIO);
    csi_gpio_init(&led_gpio, PORTA);
    csi_gpio_dir(&led_gpio, PIN24|PIN25, GPIO_DIRECTION_OUTPUT);
    csi_gpio_write(&led_gpio, PIN24|PIN25, GPIO_PIN_LOW);
}

/*===========================================================================
 * 启动确认 - 写入运行确认标记到BCB
 *===========================================================================*/
static void app_confirm_boot(void)
{
    boot_ctrl_t bcb;

    if (bcb_read(&bcb) != 0) {
        printf("[APP] WARN: Failed to read BCB\n");
        return;
    }

    /* 检查是否需要确认 */
    if (bcb.upg_state != UPG_STATE_PENDING) {
        /* 非待确认状态，无需操作 */
        return;
    }

    printf("[APP] Upgrade pending, confirming boot...\n");

    /* 设置当前分区运行确认标记 */
    if (bcb.active_part == PART_A) {
        bcb.part_a_run_confirm = RUN_CONFIRM_OK;
    } else {
        bcb.part_b_run_confirm = RUN_CONFIRM_OK;
    }

    /* 清除升级状态，复位重试计数 */
    bcb.upg_state = UPG_STATE_IDLE;
    bcb.retry_count = 0;

    if (bcb_write(&bcb) == 0) {
        printf("[APP] Boot confirmed! Upgrade successful.\n");
    } else {
        printf("[APP] ERR: Failed to write BCB\n");
    }
}

int main(void)
{
    board_init();
    led_init();
    key_init();

    /* 初始化SPI Flash (用于BCB读写) */
    csi_spiflash_qspi_init(&g_spiflash, 0, NULL);


    uint32_t run_addr = (uint32_t)(uintptr_t)&main;      // ① 取 main 函数的真实地址
    const char *slot = (run_addr < 0x18109000U) ? "A" : "B";  // ② 地址落在哪个区
	
	
    printf("=================================\n");
    printf("[APP] App started!\n");
    printf("[APP] App address: 0x%08X\n", run_addr); /* 需根据实际分区调整 */
    printf("=================================\n");

    /* 启动确认: 升级后首次启动写入确认标记 */
    app_confirm_boot();
    if(slot[0]=='A')
	{
	printf("[APP] App_A started!\n");
	 csi_gpio_write(&led_gpio, PIN25, GPIO_PIN_HIGH);
	
	}
    else{
		
	 printf("[APP] App_B started!\n");	
		
	 printf("[APP] Waiting for PB17 key press to toggle LED...\n");
      while (1) {
        if (key_pressed) {
            key_pressed = 0;
            led_state = !led_state;

            if (led_state) {
                csi_gpio_write(&led_gpio, PIN24, GPIO_PIN_HIGH);
                printf("[APP] LED ON\n");
            } else {
                csi_gpio_write(&led_gpio, PIN24, GPIO_PIN_LOW);
                printf("[APP] LED OFF\n");
            }

            mdelay(200);
        }
    }

	
	}



    return 0;
}
