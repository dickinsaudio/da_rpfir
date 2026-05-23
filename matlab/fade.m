%fade   Rasied cosine fade at start and end of a vector. 
%   y = fade(x, width, type)
%   Apply a fading curve to each of the columns of x.
%   width is either a scalar or a two element vector containing the fade edge width, either in samples
%   if >1 and as a fraction of the vector length if <1.  Width=1 will make the entire vector either
%   a fade in or a fade out.
%   type is the type of fade
%       'rcos'      Raised cosine as currently the only option is a raised cosine on the half samples
%                   sin((0.5:1:N)/N*pi/2).^2
%       'hann'      Raised cosine to match the hann window (fade(x,.5) = x.*hann(N))
%
function y = fade(x, width, type)
if (nargin<2) width = [ .1 .1 ]; end;
if (nargin<3) type  = 'rcos'; end;

if (length(width)<2) width(2) = width(1); end;
if (width(1)<=1) width(1) = round(width(1)*size(x,1)); end;
if (width(2)<=1) width(2) = round(width(2)*size(x,1)); end;
if (width(1)+width(2)>size(x,1)) error('Fade in width and fade out width exceeds length.'); end;

switch (lower(type))
    case 'rcos'
        w = [ sin((0.5:1:width(1))/width(1)*pi/2).^2 ones(1,size(x,1)-width(1)-width(2)) cos((0.5:1:width(2))/width(2)*pi/2).^2]';    
    case 'hann'
        w = [ sin((0:1:width(1)-1)/(width(1)-0.5)*pi/2).^2 ones(1,size(x,1)-width(1)-width(2)) sin((width(2)-1:-1:0)/(width(2)-0.5)*pi/2).^2]';    
    otherwise
        error('Unsupported fade type.');
end;

y = x.*w;

