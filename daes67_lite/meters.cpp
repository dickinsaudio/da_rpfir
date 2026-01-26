/*******************************************************************
 * Copyright (C) 2023 DickinsAudio
 * 
 * This source code is the proprietary information of DickinsAudio.
 * All rights reserved.
 * 
 * This code is provided under specific license agreements and is 
 * intended for evaluation and consideration for licensed use. 
 * For discussions on licensing terms and pricing, please contact 
 * info@dickins.com
 * 
 * Licensed users are permitted full use of this code for the 
 * development and building of applications and systems, including 
 * modification, extension of the code, and use and transfer within
 * alternate representations, repositories and licensing frameworks
 * as allowed by the licensing arrangements in place with 
 * DickinsAudio.
 * 
 * Any use of this code outside of evaluation, consideration for 
 * licensed use, or as aggreed by license by licensed users is 
 * strictly prohibited.
 * 
 * DickinsAudio assumes no liability, either directly or indirectly, 
 * for the use of this software in relation to the use of the software 
 * and its relationship to any third-party intellectual property.
 *******************************************************************/

#include <meters.hpp>
#include <string.h>
#include <math.h>

namespace DAES67 {
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Display - Text Meters
//
#define paint(x,y,c)    { if ((x)<nWidth && (y)<nHeight) s[(nHeight-(y)-1)*(nWidth+1)+(x)]=c; }
#define row(x,y,xx,c)   { for (int __n=(x); __n<(x)+(xx); __n++) paint(__n,y,c); }
#define col(x,y,yy,c)   { for (int __n=(y); __n<(y)+(yy); __n++) paint(x,__n,c); }
void meters(char *s, int nWidth, int nHeight, int nIn, int nOut, float *pfIn, float *pfOut)
{
	memset(s,' ',nHeight*(nWidth+1)); for (int n=0;n<nHeight;n++) { s[n*(nWidth+1)+nWidth]='\n'; };
	s[nHeight*(nWidth+1)]=0;

	int   nBar   = nHeight-3;
	float fScale;
	if (nBar>20) fScale = 0.3F;
	else if (nBar>10) fScale = 0.2F;
	else              fScale = 0.1F;

	int ni = 3;
	int no = 3+(nIn>0?(nIn+3):0);

	if (nIn>0)
	{
		col(ni,3,nBar,'|');
		col(ni+nIn+1,3,nBar,'|');
		row(ni,2,nIn+2,'-');
	}
	if (nOut>0)
	{
		col(no,3,nBar,'|');
		col(no+nOut+1,3,nBar,'|');
		row(no,2,nOut+2,'-');
	}

	for (int n=0; n<10; n++) 
	{ 
		int nH = nHeight-(int)(n*10*fScale+0.5)-1; 
		int nl = 5+nIn+nOut+(nIn>0&&nOut>0?3:0);
		if (nH<3) break;
		if (n>0) paint(0,nH,'-'); paint(1,nH,'0'+n); paint(2,nH,'0');
		if (n>0) paint(nl,nH,'-'); paint(nl+1,nH,'0'+n); paint(nl+2,nH,'0');
	}

	static char S[] = { ' ', '_', '.', 'x', 'X'};

	for (int n=0; n<nIn; n++) 
	{ 
		paint(ni+1+n,1,(n+1)%10+'0'); 
		if ((n+1)%10==0) paint(ni+1+n,0,(n+1)/10+'0');
		float h = nBar+fScale*20.0F*log10f(pfIn[n]+1E-12F);
		if (h>0)
		{
			col  (ni+1+n,3,(int)h,'X');
			paint(ni+1+n,3+(int)h,S[(int)((h-(int)h)*5)]);
		}
	};
	
	for (int n=0; n<nOut; n++) 
	{ 
		paint(no+1+n,1,(n+1)%10+'0'); 
		if ((n+1)%10==0) paint(no+1+n,0,(n+1)/10+'0');
		float h = nBar+fScale*20.0F*log10f(pfOut[n]+1E-12F);
		if (h>0)
		{
			col  (no+1+n,3,(int)h,'X');
			paint(no+1+n,3+(int)h,S[(int)((h-(int)h)*5)]);
		}
	};
}

}