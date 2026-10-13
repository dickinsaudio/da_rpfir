%%

clear all

Fs = 48000;


%%


F1 = 50;
O1 = 2;

[b a] = butter(O1,F1/(Fs/2)); 
h_1l  = impz(conv(b,b),conv(a,a),4096);
[b a] = butter(O1,F1/(Fs/2),"high"); 
h_1h  = -impz(conv(b,b),conv(a,a),4096);

F2 = 4000;
O2 = 2;
[b a] = butter(O2,F2/(Fs/2)); 
h_2l  = impz(conv(b,b),conv(a,a),4096);
[b a] = butter(O2,F2/(Fs/2),"high"); 
h_2h  = impz(conv(b,b),conv(a,a),4096);



hl = conv(h_1l, h_2l); hl = hl(1:4096);
hm = conv(h_1h, h_2l); hm = hm(1:4096);
hh = conv(h_1h, h_2h); hh = hh(1:4096);


f0 = 20;
BW_oct = 1.5;

    
    w0 = 2*pi*f0/Fs;
    Q = 1/(2*sinh(log(2)/2 * BW_oct * w0/sin(w0)));
    alpha = sin(w0)/(2*Q);
    c = cos(w0);
    b0 =  alpha;  b1 = 0;      b2 = -alpha;
    a0 = 1+alpha; a1 = -2*c;   a2 = 1-alpha;
    b = [b0 b1 b2]/a0;
    a = [1  a1/a0  a2/a0];

    %[b a] = butter(1,[10 50]/(Fs/2));
hl = hl + 2*impz(b,a,4096);


Ll = hl;
Lm = hm;
Lh = hh;



Ll  = fade(Ll(1:4096),[0 .8]);
Lm  = fade(Lm(1:4096),[0 .8]);
Lh  = fade(Lh(1:4096),[0 .8]);
%%

figure(2); clf;
Spectra([[Ll Lm Lh  Ll+Lm+Lh -Ll+Lm+Lh Ll+[zeros(34,1); Lm(1:end-34)+Lh(1:end-34)]]; zeros(100000,6)],Fs,'linewidth',2);
axis([1 (Fs/2) -40 15]);
grid on;
legend('Low','Mid','High','Phase Sum','Flip Low','Delay');


figure(4); clf;
Spectra([Ll; zeros(100000,1)],Fs,'r-','linewidth',2); hold on;
Spectra([Lm; zeros(100000,1)],Fs,'b-','linewidth',2); hold on;
Spectra([Lh; zeros(100000,1)],Fs,'m-','linewidth',2); hold on;
axis([1 (Fs/2) -40 15]);
grid on;


%% Write them out

Filters = { { "Low_LR4", Ll },
            { "Mid_LR4", Lm  },
            { "High_LR4", Lh },
            { "Passthrough", double((1:4096)'==1) }
            };



[bl al] = butter(4,22000/(Fs/2));
[bh ah] = butter(2,20/(Fs/2),'high');
figure(6); clf;
for (f=1:length(Filters))
    Filters{f}{2} = filter(bl,al,Filters{f}{2});
    Filters{f}{2} = filter(bh,ah,Filters{f}{2});
    Filters{f}{2}(abs(Filters{f}{2})<.0000000499)=0;       % Avoid the +- 0 for git diff on the Filters.h
    Spectra([ Filters{f}{2}; zeros(100000,1)], Fs); hold on;
    Spectra([ round(Filters{f}{2},5); zeros(100000,1)], Fs); hold on;
    sum(round(Filters{f}{2},8)~=0)
end;
axis([1 (Fs/2) -40 10]);
grid on;


file = fopen("../Filters.h",'w');
for (f=1:length(Filters))
    fprintf(file,"////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n");
    fprintf(file,"const float %s[] = { \n",Filters{f}{1});
    fprintf(file,"    %10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,%10.7f,\n",Filters{f}{2});
    fprintf(file,"};\n\n\n");
end;
fclose(file);

