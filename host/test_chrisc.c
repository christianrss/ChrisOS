#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../compiler/chrisc/chrisc.h"

int main(void){
    static const char ok[]=
        "void main(){\n"
        " int x=0;\n"
        " while(x<4){ pixel(x,10,12); x=x+1; wait(1); }\n"
        " if(x==4){ tone(440,3); } else { clear(0); }\n"
        "}\n";
    uint8_t code[4096];ChrisResult r;
    assert(chrisc_compile(ok,strlen(ok),code,sizeof(code),&r));
    assert(r.variables==1&&r.code_size>10);
    assert(!chrisc_compile("void main(){ y=1; }",19,code,sizeof(code),&r));
    assert(strcmp(r.diag.message,"unknown variable")==0);
    puts("test_chrisc: ok");return 0;
}
