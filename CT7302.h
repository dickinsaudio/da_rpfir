//---------------------------------------------------------------------------
#ifndef CT73xx_H_
#define CT73xx_H_

#ifdef __cplusplus
extern "C" {
#endif
	

typedef unsigned long DWORD;
typedef unsigned int  WORD;
typedef unsigned char BYTE;
typedef unsigned char U8;

#define ENABLE     1
#define DISABLE    0

#define    BYTE_TO_WORD(__X__,__Y__)    (((WORD)__X__<<8)|((WORD)__Y__))

enum
{
	SOURCE_SPIDF0=0,
	SOURCE_SPIDF1,
	SOURCE_SPIDF2,
	SOURCE_SPIDF3,
	SOURCE_SPIDF4,
	SOURCE_I2S0,
	SOURCE_I2S1,
	SOURCE_I2S2,
};

enum
{
	FREQ_UNDIFINED=0,
	FREQ_32KHZ,	FREQ_44KHZ,	FREQ_48KHZ,
	FREQ_64KHZ,	FREQ_88KHZ,	FREQ_96KHZ,
	FREQ_128KHZ,FREQ_176KHZ,FREQ_192KHZ,
	FREQ_256KHZ,FREQ_352KHZ,FREQ_384KHZ,
	FREQ_512KHZ,FREQ_704KHZ,FREQ_768KHZ,
};

void CT73xxInit(BYTE slave_addr);
void CT73xxSetI2cAddr(BYTE addr);
void CT73xxSetSoftwareMode(BYTE enable);

void CT73xxSetInputSource(BYTE index);
void CT73xxSetVolume(BYTE channel, WORD volume);
void CT73xxSetOutputFreq(BYTE index);
void CT73xxSetOutputSrc(BYTE index);
void CT73xxSetOutput32Bit(BYTE enable);
void CT73xxSetOutputD2P(BYTE enable);
void CT73xxSetPower(BYTE enable);
void CT73xxSetOutputI2sLeft(BYTE enable);
void CT73xxSetOutputI2sSlave(BYTE enable);

BYTE CT73xxCheckSignal(void);
BYTE CT73xxGetInputFreq(void);
BYTE CT73xxGetInputFormat(void);

void CT73xxSetRegisterValue(BYTE addr, BYTE value);
void CT73xxSetRegisterValueMask(BYTE addr, BYTE value, BYTE mask);
BYTE CT73xxGetRegisterValue(BYTE addr);

#ifdef __cplusplus
}
#endif

#endif	//CT73xx_H_

