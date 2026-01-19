// CONTAINS THE OPERATION OF THE CORE LOOKING AFTER AUDIO

#include "da_rpfir.hpp"
#include "i2s.pio.h"





//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// I2S AUDIO SETUP
//
// This is basically a ring buffer as seen by the SPI side of things, with the I2S being pulled and pushed by DMA.
// The DMA interrupts, but only to note timing, all the data movement is done by the DMA itself.
// The SPI routine will use the state machine address registers to determine how much of the buffer to use or fill.
//

#define     CLK_I2S         (48000)                                         // Effectively 16 channel TDM rate (but just as I2S)
#define     CLK_PIO         (2*CLK_I2S*64*4*8)                              // PIO execution rate (16 cycles each half bit of I2S)
#define     CLK_PIO_DIV_N   ((int)(CLK_SYS/CLK_PIO))                        // PIO clock divider integer part
#define     CLK_PIO_DIV_F   ((int)(((CLK_SYS%CLK_PIO)*256LL+128)/CLK_PIO))  // PIO clock divider fractional part

#define     I2S_CHANS       16                  // Number of I2S channels to output
#define     I2S_BLOCK       16                  // Number of samples at 48kHz that we lump into each ISR call
#define     I2S_BUFFER      256                 // Number of samples at 48kHz in the ring buffer  (I2S_BUFFER / I2S_BLOCK must be power of two)
#define     I2S_LINES       2                   // Number of I2S lines to use
#define     I2S_PIO         pio0                // PIO to use for I2S in and out
#define     I2S_SM0         0                   // State machine for I2S output
//#define     I2S_SM1         1                 // State machine for I2S
#define     I2S_TRIG_RING   6                   // Bit mask for trigger ring buffer - log2(BUFFER/BLOCK*4)

#include <cassert>
static_assert((1 << I2S_TRIG_RING) == (I2S_BUFFER / I2S_BLOCK * 4), "I2S_TRIG_RING must be log2(I2S_BUFFER / I2S_BLOCK * 4)");



#define     I2S_BCLK_PIN     0
#define     I2S_LRCLK_PIN    1
#define     I2S_DO0_PIN      8
#define     I2S_DO1_PIN      9
#define     I2S_DI0_PIN     10
#define     I2S_DI1_PIN     11

int32_t     i2s_in [I2S_LINES][I2S_CHANS/I2S_LINES*I2S_BUFFER];
int32_t     i2s_out[I2S_LINES][I2S_CHANS/I2S_LINES*I2S_BUFFER];

int32_t  audio_out_peaks[16] = {};              // The output audio meters

int      i2s_in0_dma1     = -1;
int      i2s_in0_dma2     = -1;
//int      i2s_in1_dma1     = -1;
//int      i2s_in1_dma2     = -1;
int      i2s_out0_dma1    = -1;
int      i2s_out0_dma2    = -1;
//int      i2s_out1_dma1    = -1;
//int      i2s_out1_dma2    = -1;

int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_in0_trigger[I2S_BUFFER/I2S_BLOCK];
//int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_in1_trigger[I2S_BUFFER/I2S_BLOCK];
int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_out0_trigger[I2S_BUFFER/I2S_BLOCK];
//int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_out1_trigger[I2S_BUFFER/I2S_BLOCK];


Histogram i2s_dma_timing("I2S DMA Timing", 0, .001F);


int dma_count = 0;
int dma_time[100];
__not_in_flash() void i2s_dma_handler(void) 
{
    dma_hw->ints0 = 1u << i2s_out0_dma2;   // Clear the interrupt request
    int64_t time = now_ns();
    i2s_dma_timing.time(time);
}

void i2s_setup()
{
    Notice("SETTING UP I2S");
    
    pio_clear_instruction_memory(I2S_PIO);
    uint offset = pio_add_program (I2S_PIO  , &tdm8_master_duplex_program);
    tdm8_master_duplex_init(I2S_PIO, I2S_SM0, offset, I2S_DI0_PIN, I2S_DO0_PIN, CLK_PIO_DIV_N, CLK_PIO_DIV_F);
//      i2s_master_out_init(I2S_PIO, 1, offset, I2S_BCLK_PIN, I2S_DO1_PIN, CLK_PIO_DIV_N, CLK_PIO_DIV_F);       // Leave off for now

    // SETUP DMA FOR I2S OUTPUT
    //
    
    i2s_out0_dma1 = dma_claim_unused_channel(true);
    i2s_out0_dma2 = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(i2s_out0_dma1);    // First DMA does the data transfer
    channel_config_set_read_increment    (&c,  true);
    channel_config_set_write_increment   (&c,  false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq              (&c, pio_get_dreq(I2S_PIO, I2S_SM0, true));
    channel_config_set_chain_to          (&c, i2s_out0_dma2);                   
    channel_config_set_high_priority     (&c, false);
    dma_channel_configure(i2s_out0_dma1, &c, &I2S_PIO->txf[I2S_SM0], i2s_out[0], I2S_CHANS*I2S_BLOCK/I2S_LINES, false);

    for (int n=0; n<I2S_BUFFER/I2S_BLOCK; n++) i2s_out0_trigger[n] = (int32_t)&i2s_out[0][n*I2S_CHANS*I2S_BLOCK/I2S_LINES];

    c = dma_channel_get_default_config   (i2s_out0_dma2);               // The second DMA does the control block
    channel_config_set_read_increment    (&c, true);                    // updating the address after each data
    channel_config_set_write_increment   (&c, false);                   // set.  Addresses should be continuous
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // and effectice ring of 2 x block
    channel_config_set_ring              (&c, false, I2S_TRIG_RING);    // The address update ring is 2^3=8 bytes (two words)
    channel_config_set_high_priority     (&c, true);
    dma_channel_configure  (i2s_out0_dma2, &c, &dma_hw->ch[i2s_out0_dma1].al3_read_addr_trig,  i2s_out0_trigger, 1, false);
    dma_channel_set_irq0_enabled(i2s_out0_dma2, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_handler);


    // SETUP DMA FOR I2S INPUT
    //

    i2s_in0_dma1 = dma_claim_unused_channel(true);
    i2s_in0_dma2 = dma_claim_unused_channel(true);  
    
    c = dma_channel_get_default_config(i2s_in0_dma1);    // First DMA does the data transfer
    channel_config_set_read_increment    (&c,  false);
    channel_config_set_write_increment   (&c,  true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq              (&c, pio_get_dreq(I2S_PIO, I2S_SM0, true));
    channel_config_set_chain_to          (&c, i2s_in0_dma2);                   
    channel_config_set_high_priority     (&c, false);
    dma_channel_configure(i2s_in0_dma1, &c, i2s_in[0], &I2S_PIO->rxf[I2S_SM0], I2S_CHANS*I2S_BLOCK/I2S_LINES, false);

    for (int n=0; n<I2S_BUFFER/I2S_BLOCK; n++) i2s_in0_trigger[n] = (int32_t)&i2s_in[0][n*I2S_CHANS*I2S_BLOCK/I2S_LINES];

    c = dma_channel_get_default_config   (i2s_in0_dma2);                // The second DMA does the control block
    channel_config_set_read_increment    (&c, true);                    // updating the address after each data
    channel_config_set_write_increment   (&c, false);                   // set.  Addresses should be continuous
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // and effectice ring of 2 x block
    channel_config_set_ring              (&c, false, I2S_TRIG_RING);    // The address update ring is 2^3=8 bytes (two words)
    channel_config_set_high_priority     (&c, true);
    dma_channel_configure  (i2s_in0_dma2, &c, &dma_hw->ch[i2s_in0_dma1].al2_write_addr_trig,  i2s_in0_trigger, 1, false);


    Notice("STARTING I2S");
    
    irq_set_priority(DMA_IRQ_0, 0x20);                  // Set I2S to highest priorit
    irq_set_enabled(DMA_IRQ_0, true);
    dma_start_channel_mask(1u << i2s_out0_dma1);        // I2S DMA
    dma_start_channel_mask(1u << i2s_in0_dma1);
    pio_enable_sm_mask_in_sync(I2S_PIO, 1 << I2S_SM0);        // Start the IS2

}




//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN AUDIO THREAD
//
//



void core_audio(void);

void start_audio()
{
    multicore_launch_core1(core_audio);
}


int64_t last_dump  = now_ns();
int64_t next_fft   = now_ns()+5000000000;
int64_t last_hist  = now_ns();

float   buf_x[4096] = { };
float   buf_y[4096] = { };
float   buf_X[4096];

#include "arm_math.h"


Histogram fft_time("FFT Time", 0, .005F);

void core_audio()
{
    Notice("CORE AUDIO STARTING"); 
    
    Notice("SETTING UP I2S");
    Notice("I2S CLOCK DESIRED:          %10d", CLK_I2S);
    Notice("PIO CLOCK DESIRED:          %10d", CLK_PIO);
    Notice("PIO CLOCK DIVIDER:        %2d + %3d/256", CLK_PIO_DIV_N, CLK_PIO_DIV_F);
    Notice("PIO CLOCK ACTUAL:           %10lld", (int64_t)(clock_get_hz(clk_sys) / ((float)CLK_PIO_DIV_N + ((float)CLK_PIO_DIV_F / 256.0f))));

    
    //i2s_setup();

    arm_rfft_fast_instance_f32 S;
    arm_status status = arm_rfft_fast_init_4096_f32(&S);
    if (status != ARM_MATH_SUCCESS) Notice("FFT INIT FAILED %d", status);
    else                          Notice("FFT INIT SUCCESS");


    printf("S fields %d %p\n", S.fftLenRFFT, S.pTwiddleRFFT);

    buf_x[0]=1.0;
    
    while(1)
    {
        int64_t now = now_ns();

        core_stall[get_core_num()].time(now); 

        if (now - last_dump > 1000130000)
        {

            // TODO Any logging here

            core_stall[get_core_num()].start();         // Avoid spurious stall measured from the printf
            last_dump = now;
        }

        if (now > next_fft)
        {
            next_fft += 2000000;

            // Reinitialize input buffer before each FFT to prevent NaN accumulation
            memset(buf_x, 0, sizeof(buf_x));
            buf_x[0] = 1.0;

            for (int n=0; n<4096; n++) buf_x[n] = rand(); 
            for (int n=0; n<4096; n++) buf_y[n] = rand();

            fft_time.start();
            arm_cmplx_mult_cmplx_f32(buf_x, buf_x, buf_X, 2048);
            //arm_rfft_fast_f32(&S, buf_x, buf_X, 0);// TODO FFT PROCESSING HERE
            fft_time.time();
        }

        if (now - last_hist > 10000000000)
        {
            last_hist = now;
            char tmp[2048];
            fft_time.sntext(2048, 15, tmp, true, 0);
            printf("FFT Time:\n %s\n", tmp);
        }
            
    }
}

