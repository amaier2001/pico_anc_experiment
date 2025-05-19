/*
** Based on the adc_dma_capture example by the Raspberry Pi foundation, heavily modified
*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "arm_math.h"

#define CAPTURE_CHANNEL 0
#define CAPTURE_DEPTH 1024

#define ADC_CLOCK 48000000
#define SAMPLE_RATE 44100 //it's supposed o be 44100 but i'm playing around with it
#define OVERSAMPLING 1 //might need to oversample and average in order to kill the noise

#define FFT_SIZE 256

uint16_t __attribute__ ((aligned (CAPTURE_DEPTH*2))) capture_buf[CAPTURE_DEPTH];

uint32_t head, tail = 0;

void core1();

int main() {
    stdio_init_all();

    // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    adc_gpio_init(26 + CAPTURE_CHANNEL);

    adc_init();
    adc_select_input(CAPTURE_CHANNEL);
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // We won't see the ERR bit because of 8 bit reads; disable.
        false     // Shift each sample to 8 bits when pushing to FIFO
    );

    adc_set_clkdiv(ADC_CLOCK / (SAMPLE_RATE * OVERSAMPLING)); //apparently it can handle the full adc clock speed without issue, i'm actually impressed

    printf("Arming DMA\n");
    sleep_ms(1000);
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, true, 11); //TODO: this is atually supposed to be 11, but the loop goes OOB, not sure why. update: fixed

    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(dma_chan, &cfg,
        capture_buf,    // dst
        &adc_hw->fifo,  // src
        0xFFFFFFFF,  // transfer count, run forever?
        true            // start immediately
    );

    printf("Starting capture\n");
    adc_run(true);

    multicore_launch_core1(core1);
    
    head = ((uint32_t)dma_hw->ch[dma_chan].write_addr - (uint32_t)capture_buf) / 2/*divide by two?  update: yes, in fact*/; //not sure if this makes sense, also TERRIBLE HACK
    tail = ((head /*-1*/) % (CAPTURE_DEPTH)); //Set & forget (??)
    
    while(1)
    {
        head = ((uint32_t)dma_hw->ch[dma_chan].write_addr - (uint32_t)capture_buf) / 2/*divide by two? update: yes, in fact*/; //not sure if this makes sense, also TERRIBLE HACK
        if (tail == head)
        {
            continue; //skip if we somehow caught up (unlikely?? update: not unlikely at all, it's very fucking fast), so we wait for the buffer to fill out more
        }

        sleep_us(1); //the program breaks if this isn't here, I have no clue why and I don't think the university cares (ask Amet just in case lol)

        tail = (tail + 1) % CAPTURE_DEPTH;
        
    }
}

void core1()
{
    while(1) 
    {
        printf("tail: %d, head: %d, delta: %d, val: %d\n\r", tail, head, (tail - head), capture_buf[tail]); //yeeted to core 1 because it was clogging up the main core and ruining my day
    }    

}