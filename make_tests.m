
Fs = 48000;
T  = (1:120*Fs)';

X = sin(T/Fs*2*pi*60).*(0.9 + 0.1*sin(T/Fs*2*pi*1)).*tukeywin(length(T),0.05);

audiowrite("Test_60Hz_2dB.wav",X,Fs,"BitsPerSample",24);