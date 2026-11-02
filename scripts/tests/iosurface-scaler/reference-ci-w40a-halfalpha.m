#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
int main(void){@autoreleasepool{
 CVPixelBufferRef p=NULL;CVReturn e=CVPixelBufferCreate(NULL,1024,1,kCVPixelFormatType_40ARGBLEWideGamut,NULL,&p);
 if(e||!p){fprintf(stderr,"CV create=%d\n",e);return 1;}
 CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 CVBufferSetAttachment(p,kCVImageBufferCGColorSpaceKey,cs,kCVAttachmentMode_ShouldPropagate);
 CVPixelBufferLockBaseAddress(p,0);uint16_t*src=CVPixelBufferGetBaseAddress(p);
 for(unsigned x=0;x<1024;x++){src[x*4]=x<<6;src[x*4+1]=384<<6;src[x*4+2]=384<<6;src[x*4+3]=639<<6;}
 CVPixelBufferUnlockBaseAddress(p,0);
 CIImage*im=[CIImage imageWithCVPixelBuffer:p options:@{kCIImageColorSpace:[NSNull null]}];
 if(!im){fprintf(stderr,"CI image unavailable\n");return 2;}
 CIContext*ctx=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES,kCIContextWorkingColorSpace:[NSNull null],kCIContextOutputColorSpace:[NSNull null],kCIContextCacheIntermediates:@NO}];
 float dst[4096];memset(dst,0xa5,sizeof(dst));
 [ctx render:im toBitmap:dst rowBytes:16384 bounds:CGRectMake(0,0,1024,1) format:kCIFormatRGBAf colorSpace:NULL];
 puts("code,R,G,B,A");for(unsigned x=0;x<1024;x++)printf("%u,%.9g,%.9g,%.9g,%.9g\n",x,dst[4*x],dst[4*x+1],dst[4*x+2],dst[4*x+3]);
 return 0;
}}
