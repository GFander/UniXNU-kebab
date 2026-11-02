#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
int main(void) {
    Boolean valid=0;
    Boolean value=CFPreferencesGetAppBooleanValue(CFSTR("CAEnableDeepFramebuffer"),kCFPreferencesCurrentApplication,&valid);
    printf("value=%d valid=%d\n",value,valid);
    return valid ? value : 2;
}
