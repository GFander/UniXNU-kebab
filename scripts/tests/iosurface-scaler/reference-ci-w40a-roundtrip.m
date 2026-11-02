#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
int main(void){@autoreleasepool{
 CVPixelBufferRef p=NULL,w=NULL,b=NULL;
 if(CVPixelBufferCreate(NULL,1024,1,kCVPixelFormatType_32BGRA,NULL,&p)||
    CVPixelBufferCreate(NULL,1024,1,kCVPixelFormatType_40ARGBLEWideGamut,NULL,&w)||
    CVPixelBufferCreate(NULL,1024,1,kCVPixelFormatType_32BGRA,NULL,&b))return 1;
 CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 for(id x in @[(__bridge id)p,(__bridge id)w,(__bridge id)b])CVBufferSetAttachment((__bridge CVPixelBufferRef)x,kCVImageBufferCGColorSpaceKey,cs,kCVAttachmentMode_ShouldPropagate);
 CVPixelBufferLockBaseAddress(p,0);uint8_t*s=CVPixelBufferGetBaseAddress(p);
 for(unsigned ch=0;ch<4;ch++)for(unsigned v=0;v<256;v++)for(unsigned k=0;k<4;k++)s[(ch*256+v)*4+k]=k==ch?v:(k==3?255:0);
 CVPixelBufferUnlockBaseAddress(p,0);
 CIContext*ctx=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES,kCIContextCacheIntermediates:@NO}];
 CIImage*im=[CIImage imageWithCVPixelBuffer:p options:@{kCIImageColorSpace:(__bridge id)cs}];
 [ctx render:im toCVPixelBuffer:w bounds:CGRectMake(0,0,1024,1) colorSpace:cs];
 CIImage*wi=[CIImage imageWithCVPixelBuffer:w options:@{kCIImageColorSpace:(__bridge id)cs}];
 [ctx render:wi toCVPixelBuffer:b bounds:CGRectMake(0,0,1024,1) colorSpace:cs];
 CVPixelBufferLockBaseAddress(w,kCVPixelBufferLock_ReadOnly);CVPixelBufferLockBaseAddress(b,kCVPixelBufferLock_ReadOnly);
 uint16_t*d=CVPixelBufferGetBaseAddress(w);uint8_t*r=CVPixelBufferGetBaseAddress(b);
 puts("channel,value,word0,word1,word2,word3,backB,backG,backR,backA");
 for(unsigned ch=0;ch<4;ch++)for(unsigned v=0;v<256;v++){unsigned i=ch*256+v;printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",ch,v,d[4*i],d[4*i+1],d[4*i+2],d[4*i+3],r[4*i],r[4*i+1],r[4*i+2],r[4*i+3]);}
 return 0;
}}
