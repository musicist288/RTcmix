#include <stdio.h>
#include <math.h>
#include "../H/complexf.h"
#include "../H/ugens.h"


void
hplset(float xxlp, float dur, float dynam, float plamp, float seed, float sr, int init, float *q)
{
/* c...............need to replace rand unit    
   c.................compile with order -lF77 -lm    
*/
	float xlp,freq,pi,w,tau,n1,ga1,x,b,s2,s1,rho,c1,pa,pc,cc,c,fl,fu,fm,rl,gl,gl2,w2,r1,r2,r;
           complex j,v1,v2,v3,v4,v5,v6,cexp(),xcmplx(),xadd(),xdivide(),xmultiply(),xsubtract(),smultiply();
	float frand(),zz;
	 int n,len,m;
#ifdef sgi
	struct __cabs_s z;
#else
/*	struct {double a,b;} z; */ /* don't need any more  -JGG */
#endif
/* printf("in hplset%f %f %f %f %f %f %d %f\n",xxlp,dur,dynam,plamp,seed,sr,init,q);
*/
      xlp=xxlp;
      freq=1./xlp;   
      pi=3.141592654;
      w=2.*pi*freq;  
      tau=dur/6.91;  
      n1=(int)(sr/freq-.5);
L21:    ga1=exp(-(n1+.5)/(tau*sr));   
      x= 1. - pow(ga1,2.);   
      b=2.*(1.-cos(w/sr));
      s2=b*b-4.*b*x ;   
      if(s2<0) goto L11;
      s1=sqrt(s2)/(2*b);  
      s1=.5+s1 ;
      rho=1.0 ; 
      if(s1<1) goto L10;
L11:   s1=.5;    
      rho=exp(-1./(tau*freq))/ABS(cos(w/(2.*sr)));  
L10:   c=1.-s1;  
      c1=s1;    
      xlp=sr/freq;   
      pa=(-1)*sr/w*atan(-c*sin(w/sr)/((1.-c)+c*cos(w/sr)));  
      pa=1.-pa ;
      n=(int)(xlp-pa);
      if(n == n1) goto L20;
      n1=n;
      goto L21;  
L20:    pc=(sr/freq)-n-pa;  
      cc=sin((w/sr-w/sr*pc)/2.)/sin((w/sr+w/sr*pc)/2.) ;
      fl=20.;   
      fu=sr/2.; 
      fm=sqrt(fl*fu);
      rl=exp(-pi*dynam/sr);											   
       j=xcmplx(0.,-1.);
       v1 = smultiply(rl,cexp(smultiply(2*pi*fm/sr,j)));
       v2 = xsubtract(xcmplx(1.,0.),v1);
#ifdef sgi
       z.a = v2.re; z.b = v2.im; 
       zz = cabs(z);
#else
       zz = cabs(v2);
#endif
       gl = (1.-rl)/zz;
       gl2=gl*gl;
      w2=w/2;  
      r1=(1.-gl2*cos(w/sr))/(1-gl2);
      r2=2*gl*sin(w2/sr)*sqrt(1.-gl2*(pow(cos(w2/sr),2)))/(1.-gl2);
      r=r1+r2; 
      if(r >= 1) r=r1-r2;
      q[1]=n+20;
      len=q[1] ; 
      if(init) {
		for(m=20; m<len; m++) {
        		seed=frand(seed);
			x=seed*2-1.;
        		q[m-1]=plamp;  
       	 		if(x < 0) q[m-1]= -plamp; 
			}
      		q[4]=0.; 
      		q[5]=0.; 
      		q[6]=0.; 
      		q[7]=0.; 
	}

      q[1]=q[1]-1;
      if (init) q[0]=q[1];
      q[2]=c ;  
      q[3]=1.-c;
      q[8]=r;  
      q[9]=rho;
      q[10]=cc ;
      q[11]=plamp;   /* for bowplucks */

    }
float frand(x)
float x;
{
      int n;
      n=x*1048576.;
      return((float)((1061*n+221589) % 1048576)/1048576.);
}
complex cexp(a)
complex a;
{
	complex b;
	b.re = exp(a.re) * cos(a.im);
	b.im = exp(a.re) * sin(a.im);
	return(b);
}
complex xcmplx(a,b)
float a,b;
{
	complex c;
	cmplx(a,b,c);
	return(c);
}
complex xdivide(a,b)
complex a,b;
{
	complex c,d,e,f;
	conjugate(b,c);
	multiply(a,c,d);
	multiply(b,c,e);
	f.re = d.re/e.re;
	f.im = d.im/e.re;
	/*prtcmplx(a); prtcmplx(b); prtcmplx(c); prtcmplx(d); prtcmplx(e); prtcmplx(f);*/
}
complex xmultiply(a,b)
complex a,b;
{
	complex c;
	c.re = a.re * b.re - a.im * b.im;
	c.im = a.re * b.im + a.im * b.re;
	return(c);
}
complex xadd(x,y)
complex x,y;
{
	complex z;
	z.re = x.re + y.re;
	z.im = x.im + y.im;
	return(z);
}
complex xsubtract(x,y)
complex x,y;
{
	complex z;
	z.re = x.re - y.re;
	z.im = x.im - y.im;
	return(z);
}
complex smultiply(x,y)
float x;
complex y;
{	complex z;
	z.re = y.re * x;
	z.im = y.im * x;
	return(z);
}

void qnew(freq2,q)                                          
float freq2,*q;
{
	float w,xlp,pa,pc,c;
	int n;
       c=q[2];
      w=2.*PI*freq2 ;                                                    
      xlp=SR/freq2;                                                      
      pa=(-1)*SR/w*atan(-c*sin(w/SR)/((1.-c)+c*cos(w/SR)));              
      n=(int)(xlp-pa);  
printf("in qnew %d %f %f %f %f %f %f %f\n",n,xlp,pa,c,SR,freq2,q[1],q[10]);                                                  
      pa=1.-pa;                                                          
      pc=(SR/freq2)-n-pa;                                                
      q[10]=sin((w/SR-w/SR*pc)/2.)/sin((w/SR+w/SR*pc)/2.);                 
      q[1]=n+19;            
printf("in qnew after changes %f %f\n",q[1],q[10]);                                             
}                                                           
