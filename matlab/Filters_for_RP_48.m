%%

clear all

Fs = 48000;


%%



N = 2048;

% Woofer protection
F1 = 50;
O1 = 3;

[b,a] = butter(O1,F1/(Fs/2),"high");
h_protect = impz(conv(b,b),conv(a,a),N);


% Woofer / tweeter crossover
% Separated LR4 corners for ~2 dB summed dip
FL = 2450;
FH = 2950;
O2 = 2;

% Woofer LR4 low-pass
[b,a] = butter(O2,FL/(Fs/2),"low");
h_low = impz(conv(b,b),conv(a,a),N);


% Tweeter target LR4 high-pass
[b,a] = butter(O2,FH/(Fs/2),"high");
bt = conv(b,b);
at = conv(a,a);


% Passive tweeter capacitor compensation
R = 3.3;
C = 10e-6;

wc = 1/(R*C);

% Physical capacitor: Hc(s) = s/(s+wc)
[bc,ac] = bilinear([1 0],[1 wc],Fs);

% Digital compensation for the capacitor
[q,rem] = deconv(bt,[1 -1]);

bd = conv(q,ac)/bc(1);
ad = at;

h_high = impz(bd,ad,N);


% Final two FIRs
hl = conv(h_protect,h_low);
hl = hl(1:N);

hh = h_high;

hl  = fade(hl,[0 .8]);
ho  = fade(-4/3*sum(hl)/length(hl)*ones(size(hl)),[0 0.8]);
hl  = hl+ho;

hh  = fade(hh,[0 .8]);


% Verification: capacitor included in tweeter
ht = filter(bc,ac,hh);
ht = ht(1:N);

%%

figure(1); clf;
Spectra([[hl hh  hl+hh hl+ht]; zeros(100000,4)],Fs,'linewidth',2);
axis([1 (Fs/2) -40 15]);
grid on;
legend('Low','High','Sum','Sum W Cap');



%% Write them out

Filters = { { "A2B_Low", hl },
            { "A2B_High", hh },
            { "Passthrough", double((1:4096)'==1) }
            };


file = fopen("../Filters.h",'w');
for (f=1:length(Filters))
    fprintf(file,"////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n");
    fprintf(file,"const float %s[] = { \n",Filters{f}{1});
    fprintf(file,"    %10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,\n",Filters{f}{2});
    fprintf(file,"};\n\n\n");
end;
fclose(file);

