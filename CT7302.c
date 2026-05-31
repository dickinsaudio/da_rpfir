#include "gpio.h"
#include "CT7302.h"
BYTE _slave_addr;

void CT73xxInit(BYTE slave_addr)
{
	U8	temp_value,time_count;
	// Set I2C Slave Addr (7 Bits)
	CT73xxSetI2cAddr(slave_addr);

	// Check for chip alive	
	delay_ms(100);
    CT73xxSetRegisterValueMask(0x2B, 0x08, 0x08);
	delay_ms(1);
    CT73xxSetRegisterValueMask(0x2B, 0x00, 0x08);

	time_count=0;
	temp_value = CT73xxGetRegisterValue(0x00);
	while(temp_value==0 && time_count++<100);
	{
		delay_ms(1);
		temp_value = CT73xxGetRegisterValue(0x00);
	}
	if(time_count<100)
	{
		CT73xxSetRegisterValue(0x00,temp_value);
	}
	printf("[%x]\r\n",temp_value);
	
	// Set Volume Normal
    CT73xxSetRegisterValueMask(0x06, 0x00, 0x80);

    CT73xxSetRegisterValue(0x10,0xD0);		//ref clk out off                     
    CT73xxSetRegisterValue(0x11,0x00);
    CT73xxSetRegisterValue(0x12,0x08);
    CT73xxSetRegisterValue(0x13,0x00);
    CT73xxSetRegisterValue(0x14,0x40);
    CT73xxSetRegisterValue(0x1C,0x03);		//MLCK 1024fs for TI DAC 	               

    CT73xxSetRegisterValue(0x30,0x23);                     
    CT73xxSetRegisterValue(0x31,0x19);                     
    CT73xxSetRegisterValue(0x32,0x1E);
	                     
    CT73xxSetRegisterValue(0x39,0xF3);
    CT73xxSetRegisterValue(0x3B,0xFF);
    CT73xxSetRegisterValue(0x40,0x02);
    CT73xxSetRegisterValue(0x45,0x00);
    CT73xxSetRegisterValue(0x4E,0x72);
	CT73xxSetRegisterValue(0x47,0xA4);		//Defualt:0x24->0xA4
//    CT73xxSetRegisterValue(0x4C,0x00);
    CT73xxSetRegisterValue(0x4D,0x37);
    CT73xxSetRegisterValue(0x59,0x2D);
    CT73xxSetRegisterValue(0x61,0x08);
    CT73xxSetRegisterValue(0x62,0x01);
	
	// Set SRC Mode 3
	CT73xxSetOutputSrc(3);
}

void CT73xxSetRegisterValue(BYTE addr, BYTE value)
{
	BYTE temp[2];
	temp[0]=addr;
	temp[1]=value;
    I2cWriteBytes(_slave_addr, 2, temp);
} 

void CT73xxSetRegisterValueMask(BYTE addr, BYTE value, BYTE mask)
{
    BYTE temp;
    temp = CT73xxGetRegisterValue(addr) & (~mask);
    CT73xxSetRegisterValue( addr, ( temp | (value & mask)));
}

BYTE CT73xxGetRegisterValue(BYTE addr)
{
	I2cWriteByte(_slave_addr, addr);
    return I2cReadByte(_slave_addr);
}

// Set I2C Slave Addr. 
void CT73xxSetI2cAddr(BYTE value)
{
    _slave_addr = value;
} 

// Select Input Source (SPDIF_0-SPDIF_4, I2S_0-I2S_2/DSD) 
void CT73xxSetInputSource(BYTE value)
{
    CT73xxSetRegisterValueMask(0x04, value, 0x07);
} 
// Get Input Source Freq. 
BYTE CT73xxGetInputFreq(void)
{
	BYTE result;
	result = CT73xxGetRegisterValue(0x89)&0x0F;
	return result;
}  
// Get Input Source Formate (DOP/32Bits) 
BYTE CT73xxGetInputFormat(void)
{
	BYTE result;
	result  = CT73xxGetRegisterValue(0x77)&0xF0;
	result |= CT73xxGetRegisterValue(0x7A)&0x0F;
	return result;
} 

// Check Input Signal 

BYTE CT73xxCheckSignal(void)
{
	BYTE value;
	value = CT73xxGetRegisterValue(0x8E)&0x02;
	return value;
} 

// Reg05[3:0]
void CT73xxSetOutputFreq(BYTE freq_idx)
{
//	freq_idx &= 0x0F;
	if(freq_idx>0)
	{
		CT73xxSetOutputSrc(3);
    	CT73xxSetRegisterValueMask(0x05, freq_idx, 0x0F);	// Output Freq
	}
	else
	{
		CT73xxSetOutputSrc(7);
	}
}

// Reg04[7:4]
void CT73xxSetOutputSrc(BYTE index)
{
    CT73xxSetRegisterValueMask(0x04, index<<4, 0xF0);
}

// Reg06[2] Only for SPDIF
void CT73xxSetOutput32Bit(BYTE enable)
{
#if 1
	if(enable)
    	CT73xxSetRegisterValueMask(0x06, 0x04, 0x04);
	else
    	CT73xxSetRegisterValueMask(0x06, 0x00, 0x04);
#else
	if(enable)
    	CT73xxSetRegisterValueMask(0x06, 0x0C, 0x0C);
	else
    	CT73xxSetRegisterValueMask(0x06, 0x00, 0x0C);
#endif
}

// Reg10[6]
void CT73xxSetOutputD2P(BYTE enable)
{
	if(enable)
    	CT73xxSetRegisterValueMask(0x59, 0x02, 0x02);
	else
    	CT73xxSetRegisterValueMask(0x59, 0x00, 0x02);
}

// Reg06[5:4]
void CT73xxSetOutputI2sLeft(BYTE enable)
{
	if(enable)
    	CT73xxSetRegisterValueMask(0x06, 0x30, 0x30);
	else
    	CT73xxSetRegisterValueMask(0x06, 0x00, 0x30);
}
// Reg06[5:4]
void CT73xxSetOutputI2sSlave(BYTE enable)
{
	if(enable)
	{
		CT73xxSetOutputSrc(0);
    	CT73xxSetRegisterValueMask(0x4D, 0x40, 0x40);
	}
	else
	{
		CT73xxSetOutputSrc(3);
    	CT73xxSetRegisterValueMask(0x4D, 0x00, 0x40);
	}
}
// Reg11[8]
void CT73xxSetPower(BYTE enable)
{
	if(enable)
    	CT73xxSetRegisterValueMask(0x11, 0x00, 0x80);
	else
    	CT73xxSetRegisterValueMask(0x11, 0x80, 0x80);
}
void CT73xxSetVolume(BYTE channel, WORD volume)
{
    if(channel==0)
    {
        CT73xxSetRegisterValue(0x09, (BYTE)(volume>>4));
        CT73xxSetRegisterValueMask(0x08, (BYTE)volume&0x0F, 0x0F);
    }
    else if(channel==1)
    {
        CT73xxSetRegisterValue(0x0A, (BYTE)(volume>>4));
        CT73xxSetRegisterValueMask(0x08, (BYTE)((volume&0x0F)<<4), 0xF0);
    }
}


