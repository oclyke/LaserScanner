#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dac8871_platform_esp32.h"

#define PIN_NUM_MOSI 13
#define PIN_NUM_CLK  14
#define PIN_NUM_LDAC 19
#define PIN_NUM_CS 21
#define PIN_NUM_RST 22

#define HOST            HSPI_HOST
#define CLOCK_FREQ      40000000    // 50 MHz max for DAC80004
#define MAX_XFER_SIZE   0           // defaults to 4096 if set to 0
#define MAX_Q_SIZE      1
#define DMA_CHANNEL     1

#define MAX_XFER_SIZE   0           // bytes. defaults to 4096 if left at 0

// Globals
dac8871_dev_t         dac = {0};
dac8871_if_esp32_arg_t  if_args = {
    .spi = NULL,
    .host = HOST,
    .spi_q_size = MAX_Q_SIZE,
    .clk_freq = CLOCK_FREQ,
    .clk_pin = PIN_NUM_CLK,
    .mosi_pin = PIN_NUM_MOSI,
    .cs_pin = PIN_NUM_CS,
    .ldac_pin = PIN_NUM_LDAC,
    .rst_pin = PIN_NUM_RST,
};
uint16_t code = 0xFFFF;

void app_main(void)
{
  dac8871_status_e dac_ret = DAC8871_STAT_OK;

  // SPI bus init may depend on other devices so it is handled separately
  spi_bus_config_t buscfg={
        .miso_io_num        = -1,
        .mosi_io_num        = if_args.mosi_pin,
        .sclk_io_num        = if_args.clk_pin,
        .quadwp_io_num      = -1,               // leave these as -1 for high speed ( > 26 MHz ) capability
        .quadhd_io_num      = -1,               // leave these as -1 for high speed ( > 26 MHz ) capability
        .max_transfer_sz    = MAX_XFER_SIZE,    // Must be at least 4, or 0 for default of 4096
  };
  ESP_ERROR_CHECK(spi_bus_initialize(if_args.host, &buscfg, DMA_CHANNEL));

  // DAC8871 operations
  dac_ret = dac8871_init_dev(&dac, &dac8871_if_esp32, &if_args);
  dac_ret = dac8871_set_code(&dac, code);     // Writes the code to the DAC8871

  while(1){
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}