% Spectra   Display the spectrum of a signal or impulse response
%
% Author:   Glenn Dickins
% Date:     26-10-2006
%
% Spectra(x,varargin)
% Spectra(x,Fs,varargin)
% Spectra(x,Fs,Fr,varargin)
%
% Displays the spectrum of the given signal or impulse response H.
% Fs is the sampling frequency
% Fr is the frequency resolution for the spectrum - fractional on log scale
% Remaining arguments are passed to the plot command
%
function Spectra(x,varargin)

Fs = 48000;
Fr = 0;

if (nargin>=2) 
    if (~isstr(varargin{1})) 
        Fs = varargin{1}; varargin={varargin{2:end}}; 
        if (nargin>=3)
            if (~isstr(varargin{1}))
                Fr = varargin{1}; varargin={varargin{2:end}};
            end;
        end;
    end;
end;

F = (0:length(x)-1)/length(x)*Fs;
%warning('Spectra Scaling Changed - June 20, 2012');     % Was previously smoothing in abs
X = abs(fft(x)).^2;                                     % domain that would bias white noise ~1dB
GaussVar=(Fr*(1:length(x))).^2;
ExponVar=GaussVar / 4;
a      =(sqrt(4*ExponVar+1)-1)/2./ExponVar;
a(~isfinite(a))=1;
for p=1:2
    S=0; for (k=1:length(X)/2)           S=(1-a(k))*S + a(k)*X(k,:); X(k,:)=S; end;
    S=0; for (k=floor(length(X)/2):-1:1) S=(1-a(k))*S + a(k)*X(k,:); X(k,:)=S; end;
end;

semilogx(F(1:floor(end/2)),10*log10(X(1:floor(end/2),:)),varargin{:});
axis([Fs/4800 Fs/2 -40 10]);


