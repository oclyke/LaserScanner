/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dacx0004_platform_esp32.h"
#include "fast_hsv2rgb.h"


#define USE_FAST_TRANSFER 0

#define LASER_CHANNEL_RED   DACX0004_ADD_A
#define LASER_CHANNEL_GREEN DACX0004_ADD_B
#define LASER_CHANNEL_BLUE  DACX0004_ADD_C

#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_LDAC 27
#define PIN_NUM_SYNC 26
#define PIN_NUM_CLR 25

#define HOST            VSPI_HOST
#define CLOCK_FREQ      40000000    // 50 MHz max for DAC80004
#define MAX_XFER_SIZE   0           // defaults to 4096 if set to 0
#define MAX_Q_SIZE      1
#define DMA_CHANNEL     1

// Application defined funcs:
void set_laser_rgb(uint16_t r, uint16_t g, uint16_t b);
#if USE_FAST_TRANSFER
void set_laser_rgb_fast(uint16_t r, uint16_t g, uint16_t b);
void finish_set_laser_rgb_fast( void );
#endif // USE_FAST_TRANSFER



// Globals
dacx0004_dev_t         dax = {0};
dax_if_esp32_arg_t  if_args = {
    .spi = NULL,
    .host = HOST,
    .spi_q_size = MAX_Q_SIZE,
    .sync_pin = PIN_NUM_SYNC,
    .ldac_pin = PIN_NUM_LDAC,
    .clr_pin = PIN_NUM_CLR,
    .mosi_pin = PIN_NUM_MOSI,
    .miso_pin = PIN_NUM_MISO,
    .clk_pin = PIN_NUM_CLK,
    .clk_freq = CLOCK_FREQ,
};

void app_main(void)
{
    dacx0004_status_e dax_ret = DACX0004_STAT_OK;
    uint8_t r, g, b;
    uint8_t s, v;
    static uint16_t h = HSV_HUE_MIN;
    s = 255;
    v = 255;

    // SPI bus init may depend on other devices so it is handled separately
    spi_bus_config_t buscfg={
            .miso_io_num        = if_args.miso_pin,
            .mosi_io_num        = if_args.mosi_pin,
            .sclk_io_num        = if_args.clk_pin,
            .quadwp_io_num      = -1,               // leave these as -1 for high speed ( > 26 MHz ) capability
            .quadhd_io_num      = -1,               // leave these as -1 for high speed ( > 26 MHz ) capability
            .max_transfer_sz    = MAX_XFER_SIZE,    // Must be at least 4, or 0 for default of 4096
    };
    ESP_ERROR_CHECK(spi_bus_initialize(if_args.host, &buscfg, DMA_CHANNEL));

    // DAX Operations
    dax_ret = dacx0004_init_dev(&dax, DAC80004, &dax_if_esp32, &if_args);
    printf("dax initialized with return code: (%d)\n", dax_ret);


    while(1){

#if USE_FAST_TRANSFER   
        set_laser_rgb_fast(((uint16_t)r << 8), ((uint16_t) g << 8), ((uint16_t) b << 8));
#else
        set_laser_rgb(((uint16_t)r << 8), ((uint16_t) g << 8), ((uint16_t) b << 8));
#endif // USE_FAST_TRANSFER

        // We could use the time in between setting the laser and finishing the transaction 
        // to calculate the next set of transactions (or rather their data)
        fast_hsv2rgb_32bit( h,  s,  v, &r, &g , &b);
        h += 1;
        if( h >= HSV_HUE_MAX){
            h = HSV_HUE_MIN;
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);

#if USE_FAST_TRANSFER 
        finish_set_laser_rgb_fast(); // waits until DMA is ready
#endif

    }

    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}










#if USE_FAST_TRANSFER
void set_laser_rgb_fast(uint16_t r, uint16_t g, uint16_t b){
    // to boost speed you could handle data transfer with a custom implementation 
    // (i.e. not using the interface functions that were defined)
    // this 'fast' function uses the automatic chip select feature of the spi driver
    // and thereby avoids a few extra function calls to 'set_sync' high and low.
    dacx0004_status_e dax_ret = DACX0004_STAT_OK;
    da80004_sr_t sr = {
        .Rw = DACX0004_RW_WRITE,
        .cmd = DACX0004_CMD_WRITEn_UPDATEn,
    };
    static bool spi_initialized = false;
    if(!spi_initialized){
        esp_err_t ret = ESP_OK;
        spi_bus_config_t buscfg={
            .miso_io_num=-1,
            .mosi_io_num=if_args.mosi_pin,
            .sclk_io_num=if_args.clk_pin,
            .quadwp_io_num=-1,
            .quadhd_io_num=-1,
            // .max_transfer_sz=MAX_XFER_SIZE,
            .max_transfer_sz=1024,
        };
        spi_device_interface_config_t devcfg={
            .clock_speed_hz=if_args.clk_freq,      // Set clock speed
            .mode=2,                                //SPI mode 2
            // .spics_io_num=-1,                       //CS pin
            .spics_io_num=PIN_NUM_CS,
            .queue_size=XAXION_Q_SIZE,              // We want to be able to queue 7 transactions at a time
            .pre_cb=NULL,                           // pre xfer cb
            .post_cb=NULL,                          // post xfer cb
        };
        //Initialize the SPI bus
        ret=spi_bus_initialize(HOST, &buscfg, 1);
        ESP_ERROR_CHECK(ret);
        //Attach the LCD to the SPI bus
        ret=spi_bus_add_device(HOST, &devcfg, &(if_args.spi));
        ESP_ERROR_CHECK(ret);
        spi_initialized = true;
    }

    const uint8_t len = 12;
    uint8_t cmds[len];

    // preformat red level
    sr.add = LASER_CHANNEL_RED;
    sr.dat = r;
    dacx0004_format_sr(&dax, sr, (uint8_t*)(cmds + 0), 4);

    // preformat blue level
    sr.add = LASER_CHANNEL_BLUE;
    sr.dat = b;
    dacx0004_format_sr(&dax, sr, (uint8_t*)(cmds + 4), 4);

    // preformat green level
    sr.add = LASER_CHANNEL_GREEN;
    sr.dat = g;
    dacx0004_format_sr(&dax, sr, (uint8_t*)(cmds + 8), 4);

    // spi_transaction_t trans[3] = {{0}, {0}, {0}}; // zeroing out the transaction was key - presumably bad data was causing havoc
    
    // for(uint8_t indi = 0; indi < 3; indi++){
    //     trans[indi].length = 4*8;
    //     trans[indi].tx_buffer = (void*)(&cmds[4*indi]);
    //     spi_device_queue_trans(if_args.spi, &trans[indi], portMAX_DELAY);
    // }
    
    spi_transaction_t trans = {
        .length = 4*8,
    };

    trans.tx_buffer = (void*)(cmds + 0);
    spi_device_queue_trans(if_args.spi, &trans, portMAX_DELAY);
    trans.tx_buffer = (void*)(cmds + 4);
    spi_device_queue_trans(if_args.spi, &trans, portMAX_DELAY);
    trans.tx_buffer = (void*)(cmds + 8);
    spi_device_queue_trans(if_args.spi, &trans, portMAX_DELAY);
}

void finish_set_laser_rgb_fast( void ){
    // Called once we are ready to wait for the SPI driver to be ready (finish sending queued transactions)
    spi_transaction_t *rtrans;
    esp_err_t ret = ESP_OK;
    //Wait for all 6 transactions to be done and get back the results.
    for (uint8_t indi=0; indi<3; indi++) {
        // ret=spi_device_get_trans_result(if_args.spi, &rtrans, portMAX_DELAY);
        assert(ret==ESP_OK);
        //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
    }
    // note: currently it appears that spi_device_get_trans_result is corrupting memory and causing a failure...
}
#else
void set_laser_rgb(uint16_t r, uint16_t g, uint16_t b){
    // this function uses a standard interface with separate functions for shifting out data and toggling the sync line
    dacx0004_status_e dax_ret = DACX0004_STAT_OK;
    da80004_sr_t sr = {
        .Rw = DACX0004_RW_WRITE,
        .cmd = DACX0004_CMD_WRITEn_UPDATEn,
    };

    // set red level
    sr.add = LASER_CHANNEL_RED;
    sr.dat = r;
    dax_ret = dacx0004_write_sr(&dax, sr);

    // set blue level
    sr.add = LASER_CHANNEL_BLUE;
    sr.dat = b;
    dax_ret = dacx0004_write_sr(&dax, sr);

    // set green level
    sr.add = LASER_CHANNEL_GREEN;
    sr.dat = g;
    dax_ret = dacx0004_write_sr(&dax, sr);
}
#endif // USE_FAST_TRANSFERs