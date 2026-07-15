#include "ieee488.h"
#include <assert.h>
#include <string.h>
/* Compile-time smoke checks for command encodings. */
int main(void){
 assert(IEEE488_LAD(5)==0x25); assert(IEEE488_TAD(5)==0x45);
 assert(IEEE488_SAD(3)==0x63); assert(IEEE488_PPE(1,0)==0x60);
 assert(IEEE488_PPE(8,1)==0x6f); assert(IEEE488_CMD_UNL==0x3f);
 return 0;
}
