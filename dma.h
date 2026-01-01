/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a DMA pair to manage a double buffered transfer
//
// Worth some notes here on RP2040
// - It is not possible to self chain DMAs, thus if only using single DMAs per PIO, you need to retrigger
//   in the interrupt
// - For most cases of I2S or TDM, the interrupt does not happen fast enough to miss the first address
//   increment of DMA, so this will skip samples
// - When using chained DMAs, the first data DMA can use a ring, however I expereinced issues with a 
//   ring size of 128, and thus have disabled it.  Leading to use a two word control block.
//
typedef enum { DMA_IN, DMA_OUT } dma_dir_t;
inline int dma_setup(pio_hw_t *pio, int sm, dma_dir_t dir, int block, int32_t *data, bool interrupt = false)
{
    static int32_t __aligned(8) Trigger[12][2];                     // Set of addresses to keep as trigger

    int dma1 = dma_claim_unused_channel(true);
    int dma2 = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(dma1);    // First DMA does the data transfer
    channel_config_set_read_increment    (&c,  dir == dma_dir_t::DMA_OUT);
    channel_config_set_write_increment   (&c, dir == dma_dir_t::DMA_IN );
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq              (&c, pio_get_dreq(pio, sm, dir == dma_dir_t::DMA_OUT));
    channel_config_set_chain_to          (&c, dma2);                   
    channel_config_set_high_priority     (&c, false);
    if (dir==dma_dir_t::DMA_OUT) dma_channel_configure(dma1, &c, &pio->txf[sm], data, block, false);
    else                         dma_channel_configure(dma1, &c, data, &pio->rxf[sm], block, false);

    Trigger[dma2][0] = (int32_t)data;                              // The addresses of the double buffer
    Trigger[dma2][1] = (int32_t)(data + block);

    c = dma_channel_get_default_config   (dma2);                    // The second DMA does the control block
    channel_config_set_read_increment    (&c, true);                // updating the address after each data
    channel_config_set_write_increment   (&c, false);               // set.  Addresses should be continuous
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);         // and effectice ring of 2 x block
    channel_config_set_ring              (&c, false, 3);            // The address update ring is 2^3=8 bytes (two words)
    channel_config_set_high_priority     (&c, false);
    if (dir==dma_dir_t::DMA_OUT) dma_channel_configure  (dma2, &c, &dma_hw->ch[dma1].al3_read_addr_trig,  Trigger[dma2], 1, false);
    else                         dma_channel_configure  (dma2, &c, &dma_hw->ch[dma1].al2_write_addr_trig, Trigger[dma2], 1, false);
    dma_channel_set_irq0_enabled(dma2, interrupt);

    return dma1;
}
