// CONTAINS THE OPERATION OF THE CORE LOOKING AFTER AUDIO

#include "da_spi2i2s.hpp"
#include "i2s.pio.h"
#include "spi.pio.h"





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
// SPI DATA SETUP
//
// SPI operates in a polled mode.  The master (RPi) sends commands, and the response gives an indication of the
// timing - the number of audio samples currently available / needed in the ring buffer.
//
// The SPI data path (PIO SM) is setup to be 24 bit - so that we are getting the 24 - 32 bit conversion on
// audio samples for free.  Thus the command structure and logic here is all 24 bits.   
//
// The data comes in on the low 24 bits of each word, and goes out from the high 24 bits of each word.
//
// The format is as follows - with IN_m_n being input sample on channel n for time m (going out to I2S) and
// OUT_m_n being output sample on channel n for time m (coming in from I2S) 
//
// INCOMING 0x00DEADmm 0x00nnxxyy IN_0_0     IN_0_1     IN_0_2     IN_0_3     IN_0_4     IN_0_5    IN_0_6 ...  A_0_(N-1)  A_1_0 ...                         ...  A_(M-1)_(N-1) PAD PAD PAD PAD PAD
// OUTGOING 0x00DEAD00 0x00BEEF00 0x00000000 0x00000000 0x00000000 0x00000000 0x000000zz OUT_0_0   OUT_0_1                      ...  B_0_(N-1)  B_1_0  ...                        B_(M-1)_(N-1)
//
// There are 8 additional words on both messages.   
//    OUTGOING 6 priming, 1 the return pointer
//    INCOMING 2 command, 5 padding
//
// mm       The number of samples in this frame (M)     
// nn       The number of channels (N)
//          The length of the frame is then 5+M*N words or (5+M*N)*3 bytes
// xx       The position to write into the I2S buffer
// yy       The position to read from the I2S buffer
// zz       The current read pointer from the outgoing I2S DMA



#define     SPI_MAX_BLOCK   64                // Max number of samples to put into a SPI block
#define     SPI_PIO         pio1
#define     SPI_SM_IN       0
#define     SPI_SM_OUT      1
#define     SPI_CHANS       16
#define     SPI_CLOCK_PIN   18
#define     SPI_MOSI_PIN    16
#define     SPI_MISO_PIN    19
#define     SPI_MAGIC       ((int32_t)0x00DAE567)
#define     SPI_HEADER       8
#define     SPI_STOKE       (SPI_HEADER-2)
#define     SPI_BITS        24
#define     SPI_RATE  50000000


int32_t __aligned(8) spi_in_trigger[2];              // Conservative large alignment
int32_t __aligned(8) spi_out_trigger[2]; 

int     spi_in_dma;
int     spi_out_dma;    

Histogram spi_blocks_timing ("SPI Blocks",   0.0F, 0.001F);
Histogram spi_dma_duration  ("SPI DMA Time", 0.0F, 0.001F);
Histogram spi_data_move_time ("SPI Data Move", 0.0F, 0.001F);

int     spi_words = 0;              // The number of words in this frame
int     spi_samples = 0;            // The number of samples in this frame
int     spi_chans = 0;              // The number of channels in this incoming frame
bool    spi_reset = false;
int64_t spi_dma_count = 0;          // Use for monitoring and timout

int32_t spi_in[SPI_MAX_BLOCK*SPI_CHANS+SPI_HEADER] = {};        // SPI in captures header and all data
int32_t spi_out[SPI_MAX_BLOCK*SPI_CHANS] = {};                  // SPI out is just data 

void spi_prime()
{
    while (pio_sm_get_tx_fifo_level(SPI_PIO, SPI_SM_OUT) < SPI_STOKE-1) pio1_hw->txf[SPI_SM_OUT] = 0x00FF0000;    
}

void __not_in_flash() spi_command_isr(void)                 // This ISR is when something is in the SPI RX FIFO
{
    if (pio_sm_get_rx_fifo_level(SPI_PIO, SPI_SM_IN)==0) return;    // It is an anomaly, but this does happen often

    while (pio_sm_get_rx_fifo_level(SPI_PIO, SPI_SM_IN))            // Rapid clear of FIFO until sync
    {
        spi_in[0] = pio1_hw->rxf[SPI_SM_IN];                        // Grab words
        if (spi_in[0]==SPI_MAGIC || spi_in[0]==0x00FFFFFF) break;   // Either valid start or reset command         
        pio1_hw->txf[SPI_SM_OUT] = 0xBAD00000;                      // Keep the output stoked
    } 

    if (spi_in[0]==0x00FFFFFF)
    {
        irq_set_enabled(PIO1_IRQ_0, false);                         // But we disable interrupts so no more reading
        spi_reset=true;
        return;
    }
    
    if (spi_in[0]!=SPI_MAGIC) return;                               // Not a valid command yet

    pio1_hw->txf[SPI_SM_OUT] = SPI_MAGIC<<8;                        // Acknowledge the frame start

    int pos = dma_hw->ch[i2s_in0_dma1].write_addr - (int32_t)i2s_in[0];
        pos = pos/(4*I2S_CHANS/I2S_LINES)*256;                       

    int n=20;
    for (; n>0; n--) if (pio_sm_get_rx_fifo_level(SPI_PIO, SPI_SM_IN)) break;
    if (n==0) return;                                               // Timeout - no more data
    
    pio1_hw->txf[SPI_SM_OUT] = pos;                                 // Send the position counter
    spi_in[1] = pio1_hw->rxf[SPI_SM_IN];                            // Grab the second word

    spi_words = spi_in[1];
    if (spi_words > SPI_CHANS*SPI_MAX_BLOCK) spi_words = 0;             // Limit to max block size
    spi_dma_count++;

    if (spi_words > 0)                                                  // No need to run output DMA if no data
    {
        dma_hw->ch[spi_out_dma].read_addr = (int32_t)spi_out;
        dma_hw->ch[spi_out_dma].al1_transfer_count_trig = spi_words;
    }
    dma_hw->ch[spi_in_dma].write_addr = (int32_t)(spi_in+2);        // Always need to capture the extra input
    dma_hw->ch[spi_in_dma].al1_transfer_count_trig = spi_words+SPI_STOKE;
    irq_set_enabled(PIO1_IRQ_0, false);                             // Disable SPI interrupts while we do DMA

    spi_chans   = SPI_CHANS;                                        // Number of channels is fixed at 16 for now
    spi_samples = spi_words / spi_chans;                             // Number of samples in this frame
    int64_t start = now_ns();
    spi_blocks_timing.time(start);                                  // Do this after the DMAs are going
    spi_dma_duration.start(start);
}


int     ring_in_pos=0;
int     ring_out_pos=8;
int     last_spi_samples=0;
int32_t ring[64][16] = {}; 
int64_t phase = 0;

void __not_in_flash() spi_dma_isr(void)                     // This ISR is when the SPI IN DMA completes
{
    dma_hw->ints1 = (1u<<spi_in_dma);
    int64_t time = now_ns();  
    spi_dma_count++;
    spi_prime();                                            // Prime the output again

    int out_samples = std::min(spi_samples+8, SPI_MAX_BLOCK);   // Extra samples for the output

    ring_out_pos = (ring_out_pos + spi_samples)%64;
    for (int n=0; n<out_samples; n++) for (int c=0; c<spi_chans; c++) spi_out[n*spi_chans+c] = ring[(ring_out_pos+n)%64][c];            // Read from behind
    for (int n=0; n<spi_samples; n++) for (int c=0; c<spi_chans; c++) ring[(ring_in_pos+n)%64][c] = spi_in[SPI_HEADER+n*spi_chans+c]<<8;
    ring_in_pos = (ring_in_pos  + spi_samples)%64;

/*
    for (int n=0; n<spi_samples; n++) 
    { 
        spi_out[n*spi_chans+2] = (int32_t)(sinf(100.0F*(float)phase * 2.0F * 3.14159265F / 48000.0F) * 0x40000000);
        phase=(phase+1)%48000;
    }
*/

    //for (int n=0; n<spi_words; n++) spi_out[n] = spi_in[n+SPI_HEADER]<<8;

#if 0    
    // Now move the data from spi_data into the I2S buffer at the right place
    spi_data_move_time.start(time);
    int write = spi_write_pos;
    int read  = spi_read_pos;
    for (int n=0; n<spi_samples; n++)
    {
        int32_t *src  = &spi_in[n*spi_chans];                       // Source data from SPI input
        int32_t *dst1 = &i2s_out[0][write*I2S_CHANS/I2S_LINES];     // The I2S output buffers
        
#if I2S_LINES==1
        dst1[0] = src[0] << 8;      // Channel 0
        dst1[1] = src[1] << 8;      // Channel 1
        dst1[2] = src[2] << 8;      // Channel 2
        dst1[3] = src[3] << 8;      // Channel 3
        dst1[4] = src[4] << 8;      // Channel 4
        dst1[5] = src[5] << 8;      // Channel 5
        dst1[6] = src[6] << 8;      // Channel 6
        dst1[7] = src[7] << 8;      // Channel 7
        dst1[8] = src[8] << 8;      // Channel 8
        dst1[9] = src[9] << 8;      // Channel 9
        dst1[10] = src[10] << 8;     // Channel 10
        dst1[11] = src[11] << 8;    // Channel 11
        dst1[12] = src[12] << 8;     // Channel 12
        dst1[13] = src[13] << 8;     // Channel 13
        dst1[14] = src[14] << 8;     // Channel 14
        dst1[15] = src[15] << 8;     // Channel 15
#endif           
#if I2S_LINES==2
        int32_t *dst2 = &i2s_out[1][write*I2S_CHANS/I2S_LINES];   
        dst1[0] = src[0] << 8;      // Channel 0
        dst1[1] = src[1] << 8;      // Channel 1
        dst1[2] = src[2] << 8;      // Channel 2
        dst1[3] = src[3] << 8;      // Channel 3
        dst1[4] = src[4] << 8;      // Channel 4
        dst1[5] = src[5] << 8;      // Channel 5
        dst1[6] = src[6] << 8;      // Channel 6
        dst1[7] = src[7] << 8;      // Channel 7
        dst2[0] = src[8] << 8;      // Channel 8
        dst2[1] = src[9] << 8;      // Channel 9
        dst2[2] = src[10] << 8;     // Channel 10
        dst2[3] = src[11] << 8;    // Channel 11
        dst2[4] = src[12] << 8;     // Channel 12
        dst2[5] = src[13] << 8;     // Channel 13
        dst2[6] = src[14] << 8;     // Channel 14
        dst2[7] = src[15] << 8;     // Channel 15
#endif
        write = (write+1)%I2S_BUFFER;

        int32_t *src1 = &i2s_in[0][read*I2S_CHANS/I2S_LINES];        // Source data from I2S input 
        int32_t *dst  = &spi_out[n*spi_chans];                       // Destination data to SPI output

#if I2S_LINES == 1
        memcpy(dst, src1, I2S_CHANS*4);                               // Channel 0-15
#endif
#if I2S_LINES == 2
        int32_t *src2 = &i2s_in[1][read*I2S_CHANS/I2S_LINES];
        memcpy(dst,               src1, I2S_CHANS/2*4);               // Channel 0-7
        memcpy(dst+I2S_CHANS/2*4, src2, I2S_CHANS/2*4);               // Channel 9-16
#endif
        read = (read+1)%I2S_BUFFER;
    }


    memcpy(&audio_out_peaks[0], i2s_out[0], 16*4);    // Update the output meters - could be done better

    spi_data_move_time.time();

#endif
    irq_set_enabled(PIO1_IRQ_0, true);                      // Re-enable SPI interrupts
    enable_interrupts();

    spi_data_move_time.start(time);
    spi_dma_duration.time(time);
    time = now_ns();
    spi_data_move_time.time(time);
}

void spi_setup()
{
    static bool initialized = false;
    pio_sm_set_enabled(SPI_PIO, SPI_SM_IN, false);      // Stop everything to make sure
    pio_sm_set_enabled(SPI_PIO, SPI_SM_OUT, false);     // we can get a clean start / reset
    irq_set_enabled(DMA_IRQ_1, false);
    irq_set_enabled(PIO1_IRQ_0, false);     

    if (!initialized)                                   // Set up all the DMA structure
    {
        Notice("SETTING UP SPI");
        initialized = true;
        spi_in_dma  = dma_claim_unused_channel(true);
        spi_out_dma = dma_claim_unused_channel(true);

        dma_channel_config c = dma_channel_get_default_config(spi_in_dma);
        channel_config_set_read_increment    (&c, false);
        channel_config_set_write_increment   (&c, true);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_dreq              (&c, pio_get_dreq(SPI_PIO, SPI_SM_IN, false));
        channel_config_set_high_priority     (&c, false);
        dma_channel_configure(spi_in_dma, &c, spi_in, &SPI_PIO->rxf[SPI_SM_IN], 0, false);      // Set transfer count set later

        c = dma_channel_get_default_config(spi_out_dma);
        channel_config_set_read_increment    (&c, true);
        channel_config_set_write_increment   (&c, false);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_dreq              (&c, pio_get_dreq(SPI_PIO, SPI_SM_OUT, true));
        channel_config_set_high_priority     (&c, false);
        dma_channel_configure(spi_out_dma, &c, &SPI_PIO->txf[SPI_SM_OUT], spi_out, 0, false);
    
        dma_channel_set_irq1_enabled(spi_in_dma, true);
        irq_set_exclusive_handler(DMA_IRQ_1, spi_dma_isr);
    }

    pio_clear_instruction_memory(SPI_PIO);                                  // Completely clear the PIO to purge any partials

    uint offset = pio_add_program(SPI_PIO, &spi_slave_in_program);
    spi_slave_in_init(SPI_PIO, SPI_SM_IN, offset, SPI_CLOCK_PIN, SPI_MOSI_PIN, 1, 0);

    offset = pio_add_program(SPI_PIO, &spi_slave_out_program);
    spi_slave_out_init(SPI_PIO, SPI_SM_OUT, offset, SPI_CLOCK_PIN, SPI_MOSI_PIN, SPI_MISO_PIN, 1, 0);
    
    pio_set_irq0_source_enabled(SPI_PIO, pis_sm0_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, spi_command_isr);                 // NOTE PIO IS EXPLICIT HERE

    hw_set_bits(&SPI_PIO->input_sync_bypass, 1u << SPI_MOSI_PIN);           // Bypass sync for SPI pins to reduce latency
    hw_set_bits(&SPI_PIO->input_sync_bypass, 1u << SPI_CLOCK_PIN);          // Needed for return to RPi

    irq_set_priority(PIO1_IRQ_0, 0x00);                 // Respond to SPI bytes immediately
    irq_set_priority(DMA_IRQ_1, 0x40);                  // Set SPI below
    irq_set_enabled(PIO1_IRQ_0, true);                  // NOTE PIO IS EXPLICIT HERE
    irq_set_enabled(DMA_IRQ_1, true);

    spi_dma_count = (spi_dma_count+1)&0xFFFFFFFFFFFFFFFE;   // Make sure we are even
    
    pio_enable_sm_mask_in_sync(SPI_PIO, (1 << SPI_SM_IN) | (1 << SPI_SM_OUT));        // Start the SPI in sync
    spi_prime();                                        // Prime

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

void core_audio()
{
    Notice("CORE AUDIO STARTING"); 
    
    Notice("SETTING UP I2S");
    Notice("I2S CLOCK DESIRED:          %10d", CLK_I2S);
    Notice("PIO CLOCK DESIRED:          %10d", CLK_PIO);
    Notice("PIO CLOCK DIVIDER:        %2d + %3d/256", CLK_PIO_DIV_N, CLK_PIO_DIV_F);
    Notice("PIO CLOCK ACTUAL:           %10lld", (int64_t)(clock_get_hz(clk_sys) / ((float)CLK_PIO_DIV_N + ((float)CLK_PIO_DIV_F / 256.0f))));

    
    i2s_setup();
    spi_setup();


    while(1)
    {
        int64_t now = now_ns();
        if (spi_reset)
        {
            spi_reset = false;
            spi_setup();
            Notice("SPI RESET TRIGGERED");
        }

        core_stall[get_core_num()].time(now); 

        if (now - last_dump > 1000130000)
        {
            //Notice("AUDIO %08X %08X %08X %08X %08X %08X %08X %08X ",spi_in[0], spi_in[1], spi_in[2], spi_in[3],spi_in[4], spi_in[5], spi_in[6], spi_in[7]);
            //Notice("SPI SM PC   %d   SPI_FIFO %d    SPI_COMMAND %lld",pio_sm_get_pc(SPI_PIO, SPI_SM), pio_sm_get_rx_fifo_level(SPI_PIO, SPI_SM), spi_command_timing.N());
            //Notice("I2S PIO PROGRAM COUNTER %d", I2S_PIO->sm[0].addr);
            //Notice("I2S OUT0 STATUS: 1 %08X 2 %08X    I2S IN0 STATUS:  1 %08X 2 %08X     DIFF %d", (int32_t)dma_hw->ch[i2s_out0_dma1].read_addr-(int32_t)i2s_out, (int32_t)dma_hw->ch[i2s_out0_dma2].read_addr-(int32_t)i2s_out0_trigger,
            //                                                                             (int32_t)dma_hw->ch[i2s_in0_dma1].write_addr-(int32_t)i2s_in, (int32_t)dma_hw->ch[i2s_in0_dma2].read_addr-(int32_t)i2s_in0_trigger,
            //                                                                            (int32_t)dma_hw->ch[i2s_out0_dma1].read_addr-(int32_t)i2s_out[0] - (int32_t)dma_hw->ch[i2s_in0_dma1].write_addr+(int32_t)i2s_in[0]);    
            core_stall[get_core_num()].start();         // Avoid spurious stall measured from the printf
            last_dump = now;
            //printf("IGNORES  %08x %08X %08X %08X %08X %08X %08X %08X\n", ignores[7], ignores[6], ignores[5], ignores[4], ignores[3], ignores[2], ignores[1], ignores[0]);
            //printf("SPI SM FIFO DEPTHS IN:%d OUT:%d    ",  pio_sm_get_rx_fifo_level(pio1, 0), pio_sm_get_tx_fifo_level(pio1, 1));
            //printf("DMA Pointers       IN %03d   OUT %03d  \n", dma_hw->ch[spi_in_dma].write_addr - (int32_t)spi_data,
            //                                                 dma_hw->ch[spi_out_dma].read_addr  - (int32_t)spi_data);
            //printf("DMA COUNT %6lld  TXFIFO %d    SPI_IN  %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n", spi_dma_count, pio_sm_get_tx_fifo_level(SPI_PIO, SPI_SM_OUT), spi_in[0], spi_in[1], spi_in[2], spi_in[3], spi_in[4], spi_in[5], spi_in[6], spi_in[7], spi_in[8], spi_in[9]);
            //printf("DMA COUNT         TXFIFO      SPI_OUT %08X %08X %08X %08X %08X %08X %08X %08X\n", spi_out[0], spi_out[1], spi_out[2], spi_out[3], spi_out[4], spi_out[5], spi_out[6], spi_out[7]);
            //char tmp[2000];
            //spi_read_fifo_level.sntext(2000,15,tmp);
            //printf("SPI RX FIFO LEVEL HISTOGRAM\n%s", tmp);


        }
            
    }
}

