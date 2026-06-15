%%
MAX_V = 43;             % Maximum voltage to ask for 
MIN_V = 43;            % Minimum voltage - use digital gain from here down
MAX_Gain = 0.2;         % Maximum dB to apply to avoid pushing amp to rails

MIN_VOL = 80;
LIN_STEPS = 60;         % From here to 100 is linear steps
LIN_RANGE = 30;         % This is the volume spread from LIN_FROM to 100

LIN_STEP = LIN_RANGE/LIN_STEPS;

Vsteps = floor(log(MAX_V/MIN_V) / log(10^(LIN_STEP/20)))+1;
V = MAX_V * 10.^(-(0:1:Vsteps-1)*LIN_STEP/20);
V(end+1:101)=V(end);

G(1:Vsteps)=MAX_Gain;
G(Vsteps:LIN_STEPS)=MAX_Gain + (0:(LIN_STEPS-Vsteps))*LIN_STEP;


y0 = G(LIN_STEPS);
m0 = LIN_STEP;
y1 = MIN_VOL-20*log10(V(1)/V(end));
x = 0:(100 - LIN_STEPS);
a = (y1 - y0 - m0 * x(end)) / (x(end)^2);
G(LIN_STEPS:100) = y0 + m0 * x + a * x.^2;
G(101)=200;

X = [ V; G ];
fprintf("  { %8.3fF, %6.2fF }, \n",X(:));

plot(20*log10(V(1:100)/MAX_V)-G(1:100));
