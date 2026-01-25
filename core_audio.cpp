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

#define     CLK_I2S         (96000)                                         
#define     CLK_PIO         (CLK_I2S*64*16)                                 // PIO execution rate (16 cycles each bit of I2S)
#define     CLK_PIO_DIV_N   ((int)(CLK_SYS/CLK_PIO))                        // PIO clock divider integer part
#define     CLK_PIO_DIV_F   ((int)(((CLK_SYS%CLK_PIO)*256LL+128)/CLK_PIO))  // PIO clock divider fractional part

#define     I2S_CHANS       2                   // Number of I2S channels to output
#define     I2S_BLOCK       2048                // Number of samples at that we lump into each ISR call
#define     I2S_BUFFER      2*I2S_BLOCK         // Number of samples at in the ring buffer  (I2S_BUFFER / I2S_BLOCK must be power of two)
#define     I2S_PIO         pio0                // PIO to use for I2S in and out
#define     I2S_OUT0_SM     0                   // State machine for I2S output
#define     I2S_OUT1_SM     1                   // State machine for I2S
#define     I2S_IN_SM       2                   // State machine for I2S input
#define     I2S_TRIG_RING   3                   // Bit mask for trigger ring buffer - log2(BUFFER/BLOCK*4)

#include <cassert>
static_assert((1 << I2S_TRIG_RING) == (I2S_BUFFER / I2S_BLOCK * 4), "I2S_TRIG_RING must be log2(I2S_BUFFER / I2S_BLOCK * 4)");


#define     I2S_IN_BCLK_PIN     6
#define     I2S_IN_LRCLK_PIN    7
#define     I2S_IN_SD_PIN       5
#define     I2S_OUT_BCLK_PIN    2
#define     I2S_OUT_LRCLK_PIN   3
#define     I2S_OUT_SD0_PIN     1
#define     I2S_OUT_SD1_PIN     0

int32_t     i2s_in [I2S_CHANS*I2S_BUFFER];
int32_t     i2s_out0[I2S_CHANS*I2S_BUFFER]  = { (int32_t)0xFF00FF00, (int32_t)0xCCCCCCCC };
//int32_t     i2s_out1[I2S_CHANS*I2S_BUFFER];

int32_t  audio_in_peaks[2] = {};              // The output audio meters
int32_t  audio_out_peaks[4] = {};             // The output audio meters

int      i2s_in_dma1      = -1;
int      i2s_in_dma2      = -1;
int      i2s_out0_dma1    = -1;
int      i2s_out0_dma2    = -1;
int      i2s_out1_dma1    = -1;
int      i2s_out1_dma2    = -1;

int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_in_trigger[I2S_BUFFER/I2S_BLOCK];
int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_out0_trigger[I2S_BUFFER/I2S_BLOCK];
//int32_t __aligned(I2S_BUFFER/I2S_BLOCK*4) i2s_out1_trigger[I2S_BUFFER/I2S_BLOCK];


Histogram i2s_in_dma_timing("I2S DMA Timing", 0, .050F);
Histogram i2s_out0_dma_timing("I2S DMA Timing", 0, .050F);
Histogram i2s_out1_dma_timing("I2S DMA Timing", 0, .050F);


__not_in_flash() void i2s_dma_handler(void) 
{
    dma_hw->ints0 = 1u << i2s_in_dma2;   // Clear the interrupt request
    int64_t time = now_ns();
    i2s_out0_dma_timing.time(time);
}

void i2s_setup()
{
    Notice("SETTING UP I2S DMA");
    
    pio_clear_instruction_memory(I2S_PIO);
    uint offset = pio_add_program (I2S_PIO  , &i2s_master_out_program);
    i2s_master_out_init(I2S_PIO, I2S_OUT0_SM, offset, I2S_OUT_BCLK_PIN, I2S_OUT_SD0_PIN, CLK_PIO_DIV_N, CLK_PIO_DIV_F);   

    offset = pio_add_program (I2S_PIO  , &i2s_in_program);  
    i2s_in_init(I2S_PIO, I2S_IN_SM, offset, I2S_IN_LRCLK_PIN, I2S_IN_SD_PIN, CLK_PIO_DIV_N, CLK_PIO_DIV_F);
    
    // SETUP DMA FOR I2S OUTPUT
    //
    
    i2s_out0_dma1 = dma_claim_unused_channel(true);
    i2s_out0_dma2 = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(i2s_out0_dma1);    // First DMA does the data transfer
    channel_config_set_read_increment    (&c,  true);
    channel_config_set_write_increment   (&c,  false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq              (&c, pio_get_dreq(I2S_PIO, I2S_OUT0_SM, true));
    channel_config_set_chain_to          (&c, i2s_out0_dma2);                   
    channel_config_set_high_priority     (&c, false);
    dma_channel_configure(i2s_out0_dma1, &c, &I2S_PIO->txf[I2S_OUT0_SM], i2s_out0, I2S_CHANS*I2S_BLOCK, false);

    for (int n=0; n<I2S_BUFFER/I2S_BLOCK; n++) i2s_out0_trigger[n] = (int32_t)&i2s_out0[n*I2S_CHANS*I2S_BLOCK];

    c = dma_channel_get_default_config   (i2s_out0_dma2);               // The second DMA does the control block
    channel_config_set_read_increment    (&c, true);                    // updating the address after each data
    channel_config_set_write_increment   (&c, false);                   // set.  Addresses should be continuous
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // and effectice ring of 2 x block
    channel_config_set_ring              (&c, false, I2S_TRIG_RING);    // The address update ring is 2^3=8 bytes (two words)
    channel_config_set_high_priority     (&c, true);
    dma_channel_configure  (i2s_out0_dma2, &c, &dma_hw->ch[i2s_out0_dma1].al3_read_addr_trig,  i2s_out0_trigger, 1, false);

    printf("INITIAL DMA Pointers I2S OUT0: IN %p OUT %p\n", (void*)dma_hw->ch[i2s_out0_dma1].read_addr, (void*)dma_hw->ch[i2s_out0_dma1].write_addr);

    
    // SETUP DMA FOR I2S INPUT
    //

    i2s_in_dma1 = dma_claim_unused_channel(true);
    i2s_in_dma2 = dma_claim_unused_channel(true);  
    
    c = dma_channel_get_default_config(i2s_in_dma1);    // First DMA does the data transfer
    channel_config_set_read_increment    (&c,  false);
    channel_config_set_write_increment   (&c,  true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq              (&c, pio_get_dreq(I2S_PIO, I2S_IN_SM, false));
    channel_config_set_chain_to          (&c, i2s_in_dma2);                   
    channel_config_set_high_priority     (&c, false);
    dma_channel_configure(i2s_in_dma1, &c, i2s_in, &I2S_PIO->rxf[I2S_IN_SM], I2S_CHANS*I2S_BLOCK, false);

    for (int n=0; n<I2S_BUFFER/I2S_BLOCK; n++) i2s_in_trigger[n] = (int32_t)&i2s_in[n*I2S_CHANS*I2S_BLOCK];

    c = dma_channel_get_default_config   (i2s_in_dma2);                // The second DMA does the control block
    channel_config_set_read_increment    (&c, true);                    // updating the address after each data
    channel_config_set_write_increment   (&c, false);                   // set.  Addresses should be continuous
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // and effectice ring of 2 x block
    channel_config_set_ring              (&c, false, I2S_TRIG_RING);    // The address update ring is 2^3=8 bytes (two words)
    channel_config_set_high_priority     (&c, true);
    dma_channel_configure  (i2s_in_dma2, &c, &dma_hw->ch[i2s_in_dma1].al2_write_addr_trig,  i2s_in_trigger, 1, false);

    printf("INITIAL DMA Pointers I2S IN: IN %p OUT %p\n", (void*)dma_hw->ch[i2s_in_dma1].read_addr, (void*)dma_hw->ch[i2s_in_dma1].write_addr);
    printf("INITIAL PIO0 I2S IN SM PC: %d \n", pio_sm_get_pc(I2S_PIO, I2S_IN_SM));


    Notice("STARTING I2S");

    dma_channel_set_irq0_enabled(i2s_in_dma2, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_handler);
    irq_set_priority(DMA_IRQ_0, 0x20);                  // Set I2S to highest priority
    irq_set_enabled(DMA_IRQ_0, true);
    dma_start_channel_mask(1u << i2s_out0_dma1);
    dma_start_channel_mask(1u << i2s_in_dma1);
    sleep_us(20);

    pio_enable_sm_mask_in_sync(I2S_PIO, (1 << I2S_OUT0_SM) | (1 << I2S_IN_SM) );        // Start the IS2
//    pio_enable_sm_mask_in_sync(I2S_PIO, (1 << I2S_OUT0_SM));

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

#define T_FFT       I2S_BLOCK       // The stride of time samples between each FFT
#define N_FFT       T_FFT           // The size of the complex FFT we will be using
#define M_FIR       4               // The number of blocks to use for filtering
#define CHANS       2               // The number of output channels

#include "arm_math.h"
                                                    
float32_t   buf_x[2*N_FFT] = { };                   
float32_t   buf_y[CHANS][2*N_FFT] = { };            
float32_t   buf_X[M_FIR][2*N_FFT];
float32_t   buf_H[CHANS][M_FIR][2*N_FFT];
float32_t   buf_Y[2*N_FFT];
float32_t   buf_tmp[2*N_FFT];

Histogram fft_time("DSP Time", 0, .050F);

void core_audio()
{
    Notice("CORE AUDIO STARTING"); 
    
    Notice("SETTING UP I2S CLOCKS");
    Notice("I2S CLOCK DESIRED:          %10d", CLK_I2S);
    Notice("PIO CLOCK DESIRED:          %10d", CLK_PIO);
    Notice("PIO CLOCK DIVIDER:        %2d + %3d/256", CLK_PIO_DIV_N, CLK_PIO_DIV_F);
    Notice("PIO CLOCK ACTUAL:           %10lld", (int64_t)(clock_get_hz(clk_sys) / ((float)CLK_PIO_DIV_N + ((float)CLK_PIO_DIV_F / 256.0f))));

    
    i2s_setup();

    arm_rfft_fast_instance_f32 S;
    arm_status status = arm_rfft_fast_init_4096_f32(&S);
    if (status != ARM_MATH_SUCCESS) Notice("FFT INIT FAILED %d", status);
    else                          Notice("FFT INIT SUCCESS");

    for (int n=0; n<(int)(sizeof(buf_H)/sizeof(float32_t)); n++) (&buf_H[0][0][0])[n] = rand(); 
    while(1)
    {
        int64_t now = now_ns();

        core_stall[get_core_num()].time(now); 

        if (now - last_dump > 1000130000)
        {

            // TODO Any logging here
            // printf("DMA Pointers I2S IN0: IN %p OUT %p\n", (void*)dma_hw->ch[i2s_in_dma1].read_addr, (void*)dma_hw->ch[i2s_in_dma1].write_addr);
            printf("Incoming data %08x %08x %08x %08x %08x %08x %08x %08x\n",
                ((uint32_t*)i2s_in)[0], ((uint32_t*)i2s_in)[1], ((uint32_t*)i2s_in)[2], ((uint32_t*)i2s_in)[3],
                ((uint32_t*)i2s_in)[4], ((uint32_t*)i2s_in)[5], ((uint32_t*)i2s_in)[6], ((uint32_t*)i2s_in)[7]
            );
            


            core_stall[get_core_num()].start();         // Avoid spurious stall measured from the printf
            last_dump = now;
        }

        if (now > next_fft)
        {
            next_fft += 21000000;

            fft_time.start();
            // Reinitialize input buffer before each FFT to prevent NaN accumulation

            memmove(buf_x, buf_x+T_FFT, 2*N_FFT -  T_FFT);
            for (int n=0; n<T_FFT; n++) buf_x[2*N_FFT - T_FFT + n] = rand(); 

            static int m=0;
            arm_rfft_fast_f32(&S, buf_x, buf_X[m], 0);   m = (m+1)%M_FIR;
            for (int i=0; i<CHANS; i++)
            {   
                memset(buf_Y, 0, sizeof(buf_Y));
                for (int n=0; n<M_FIR; n++)
                {
                    arm_cmplx_mult_cmplx_f32(buf_X[n], buf_H[i][n], buf_tmp, 2048);
                    if (n>0) arm_add_f32(buf_Y, buf_tmp, buf_Y, 2*N_FFT);
                }
                arm_rfft_fast_f32(&S, buf_tmp, buf_Y, 1);
                memmove(buf_y[i], buf_y[i]+T_FFT, 2*N_FFT -  T_FFT);
                for (int n=0; n<T_FFT; n++) buf_y[i][2*N_FFT - T_FFT + n] = buf_Y[n];
            }
            fft_time.time();
        }
    }
}

