%%

clear all

Slr = load('20260216_LOW.txt');
Slr = Slr(:,2);

Smr = load('20260216_MID.txt');
Smr = Smr(:,2);

Shr = load('20260216_HI.txt');
Shr = Shr(:,2);

%%
Sl = Slr(mod((-20:end-21)+length(Slr),length(Slr))+1);
Sm = Smr(mod((-20:end-21)+length(Slr),length(Slr))+1);
Sh = Shr(mod((-20:end-21)+length(Slr),length(Slr))+1);
%Sl = Sl(1:length(Sl)/2);
%Sm = Sm(1:length(Sm)/2);
%Sh = Sh(1:length(Sh)/2);





F1 = 1350;
O1 = 1;

[b a] = butter(O1,F1/48000); 
h_1l  = impz(conv(b,b),conv(a,a),4096);
[b a] = butter(O1,F1/48000,"high"); 
h_1h  = -impz(conv(b,b),conv(a,a),4096);

F2 = 4000;
O2 = 2;
[b a] = butter(O2,F2/48000); 
h_2l  = impz(conv(b,b),conv(a,a),4096);
[b a] = butter(O2,F2/48000,"high"); 
h_2h  = impz(conv(b,b),conv(a,a),4096);



hl = 0.9*conv(h_1l, h_2l); hl = hl(1:4096);
hm = 1.1*conv(h_1h, h_2l); hm = hm(1:4096);
hh =      conv(h_1h, h_2h); hh = hh(1:4096);


f0 = 20;
BW_oct = 1.5;
Fs = 96000;
    
    w0 = 2*pi*f0/Fs;
    Q = 1/(2*sinh(log(2)/2 * BW_oct * w0/sin(w0)));
    alpha = sin(w0)/(2*Q);
    c = cos(w0);
    b0 =  alpha;  b1 = 0;      b2 = -alpha;
    a0 = 1+alpha; a1 = -2*c;   a2 = 1-alpha;
    b = [b0 b1 b2]/a0;
    a = [1  a1/a0  a2/a0];

    %[b a] = butter(1,[10 50]/48000);
hl = hl + 2*impz(b,a,4096);


Ll = hl;
Lm = hm;
Lh = hh;


[b a] = butter(2,5/48000,'high');
Slt = filter(b,a,filter(b,a,Sl));
Ll  = filter(b,a,Ll);

Slt = fade(Slt(1:4096),[20 .8]);
Smt = fade(Sm(1:4096),[20 .8]);
Sht = fade(Sh(1:4096),[20 .8]);

Ll  = fade(Ll(1:4096),[0 .8]);
Lm  = fade(Lm(1:4096),[0 .8]);
Lh  = fade(Lh(1:4096),[0 .8]);

DC = -2*sum(Slt)/length(Slt)*tukeywin(length(Slt),1);
Slt = Slt + DC;

DC = -2*sum(Ll)/length(Ll)*tukeywin(length(Ll),1);
Ll = Ll + DC;

figure(1); clf;
Spectra([[Ll Lm Lh Ll+Lm+Lh]; zeros(100000,4)],96000,'linewidth',2);
axis([1 48000 -40 15]);
grid on;
legend('Low','Mid','High','Phase Sum');




Gl = 10^(3/20);
Gm = 10^(-3.5/20);
Gh = 10^(-8/20);

figure(2); clf;
Spectra([[Sl/Gl Sm/Gm Sh/Gh Sl/Gl+Sm/Gm+Sh/Gh -Sl/Gl+Sm/Gm+Sh/Gh Sl/Gl+[zeros(34,1); Sm(1:end-34)/Gm+Sh(1:end-34)/Gh]]; zeros(100000,6)],96000,'linewidth',2);
axis([1 48000 -40 15]);
grid on;
legend('Low','Mid','High','Phase Sum','Flip Low','Delay');



Ll = Gl*Ll;
Lm = Gm*Lm;
Lh = Gh*Lh;


Scale = 1/2;

Sl = Scale*Sl;
Sm = Scale*Sm;
Sh = Scale*Sh;
Slt = Scale*Slt;
Smt = Scale*Smt;
Sht = Scale*Sht;
Ll = Scale*Ll;
Lm = Scale*Lm;
Lh = Scale*Lh;




figure(4); clf;
Spectra([Sl; zeros(100000,1)],96000,'r','linewidth',3); hold on;
Spectra([Ll; zeros(100000,1)],96000,'r--','linewidth',2); hold on;
Spectra([Slt; zeros(100000,1)],96000,'k:','linewidth',2); hold on;
Spectra([Sm; zeros(100000,1)],96000,'b','linewidth',3); hold on;
Spectra([Lm; zeros(100000,1)],96000,'b--','linewidth',2); hold on;
Spectra([Smt; zeros(100000,1)],96000,'k:','linewidth',2); hold on;
Spectra([Sh; zeros(100000,1)],96000,'m','linewidth',3); hold on;
Spectra([Lh; zeros(100000,1)],96000,'m--','linewidth',2); hold on;
Spectra([Sht; zeros(100000,1)],96000,'k:','linewidth',2); hold on;
legend('Analog','Hand Tuned LR4','RP Simulate Analog');
axis([1 48000 -40 15]);
grid on;


%% Write them out

Filters = { { "SGR_Low_LR4", Ll },
            { "SGR_Mid_LR4", Lm  },
            { "SGR_High_LR4", Lh },
            { "Passthrough", double((1:4096)'==1) },
            { "SGR_Low_SGR", Slt },
            { "SGR_Mid_SGR", Smt },
            { "SGR_High_SGR", Sht },
            };



[bl al] = butter(4,35000/48000);
[bh ah] = butter(2,20/48000,'high');
figure(6); clf;
for (f=1:length(Filters))
    Filters{f}{2} = filter(bl,al,Filters{f}{2});
    Filters{f}{2} = filter(bh,ah,Filters{f}{2});
    Filters{f}{2}(abs(Filters{f}{2})<.0000001)=0;       % Avoid the +- 0 for git diff on the Filters.h
    Spectra([ Filters{f}{2}; zeros(100000,1)], 96000); hold on;
    Spectra([ round(Filters{f}{2},5); zeros(100000,1)], 96000); hold on;
    sum(round(Filters{f}{2},8)~=0)
end;
axis([1 48000 -40 10]);
grid on;


file = fopen("../Filters.h",'w');
for (f=1:length(Filters))
    fprintf(file,"////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n");
    fprintf(file,"const float %s[] = { \n",Filters{f}{1});
    fprintf(file,"    %10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,\n",Filters{f}{2});
    fprintf(file,"};\n\n\n");
end;
fclose(file);

