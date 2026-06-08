/*
%%
MAX_V = 42;             % Maximum voltage to ask for 
MIN_V = 5.5;            % Minimum voltage - use digital gain from here down
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

*/

typedef struct volume_table_entry {
    float voltage;
    float gain;
} volume_table_entry_t;

static volume_table_entry_t s_volume_table[101] = 
{
  {   42.000F,   0.20F }, 
  {   39.651F,   0.20F }, 
  {   37.433F,   0.20F }, 
  {   35.339F,   0.20F }, 
  {   33.362F,   0.20F }, 
  {   31.496F,   0.20F }, 
  {   29.734F,   0.20F }, 
  {   28.070F,   0.20F }, 
  {   26.500F,   0.20F }, 
  {   25.018F,   0.20F }, 
  {   23.618F,   0.20F }, 
  {   22.297F,   0.20F }, 
  {   21.050F,   0.20F }, 
  {   19.872F,   0.20F }, 
  {   18.761F,   0.20F }, 
  {   17.711F,   0.20F }, 
  {   16.721F,   0.20F }, 
  {   15.785F,   0.20F }, 
  {   14.902F,   0.20F }, 
  {   14.069F,   0.20F }, 
  {   13.282F,   0.20F }, 
  {   12.539F,   0.20F }, 
  {   11.837F,   0.20F }, 
  {   11.175F,   0.20F }, 
  {   10.550F,   0.20F }, 
  {    9.960F,   0.20F }, 
  {    9.403F,   0.20F }, 
  {    8.877F,   0.20F }, 
  {    8.380F,   0.20F }, 
  {    7.911F,   0.20F }, 
  {    7.469F,   0.20F }, 
  {    7.051F,   0.20F }, 
  {    6.657F,   0.20F }, 
  {    6.284F,   0.20F }, 
  {    5.933F,   0.20F }, 
  {    5.601F,   0.20F }, 
  {    5.601F,   0.70F }, 
  {    5.601F,   1.20F }, 
  {    5.601F,   1.70F }, 
  {    5.601F,   2.20F }, 
  {    5.601F,   2.70F }, 
  {    5.601F,   3.20F }, 
  {    5.601F,   3.70F }, 
  {    5.601F,   4.20F }, 
  {    5.601F,   4.70F }, 
  {    5.601F,   5.20F }, 
  {    5.601F,   5.70F }, 
  {    5.601F,   6.20F }, 
  {    5.601F,   6.70F }, 
  {    5.601F,   7.20F }, 
  {    5.601F,   7.70F }, 
  {    5.601F,   8.20F }, 
  {    5.601F,   8.70F }, 
  {    5.601F,   9.20F }, 
  {    5.601F,   9.70F }, 
  {    5.601F,  10.20F }, 
  {    5.601F,  10.70F }, 
  {    5.601F,  11.20F }, 
  {    5.601F,  11.70F }, 
  {    5.601F,  12.20F }, 
  {    5.601F,  12.72F }, 
  {    5.601F,  13.28F }, 
  {    5.601F,  13.87F }, 
  {    5.601F,  14.50F }, 
  {    5.601F,  15.17F }, 
  {    5.601F,  15.88F }, 
  {    5.601F,  16.63F }, 
  {    5.601F,  17.41F }, 
  {    5.601F,  18.23F }, 
  {    5.601F,  19.09F }, 
  {    5.601F,  19.99F }, 
  {    5.601F,  20.93F }, 
  {    5.601F,  21.90F }, 
  {    5.601F,  22.91F }, 
  {    5.601F,  23.96F }, 
  {    5.601F,  25.05F }, 
  {    5.601F,  26.17F }, 
  {    5.601F,  27.34F }, 
  {    5.601F,  28.54F }, 
  {    5.601F,  29.77F }, 
  {    5.601F,  31.05F }, 
  {    5.601F,  32.37F }, 
  {    5.601F,  33.72F }, 
  {    5.601F,  35.11F }, 
  {    5.601F,  36.54F }, 
  {    5.601F,  38.00F }, 
  {    5.601F,  39.51F }, 
  {    5.601F,  41.05F }, 
  {    5.601F,  42.63F }, 
  {    5.601F,  44.24F }, 
  {    5.601F,  45.90F }, 
  {    5.601F,  47.59F }, 
  {    5.601F,  49.32F }, 
  {    5.601F,  51.09F }, 
  {    5.601F,  52.90F }, 
  {    5.601F,  54.74F }, 
  {    5.601F,  56.63F }, 
  {    5.601F,  58.55F }, 
  {    5.601F,  60.50F }, 
  {    5.601F,  62.50F }, 
  {    5.601F, 200.00F }
};
