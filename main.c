#include "cdc.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    cdc_init();
    
    if(argc != 2)
    {
        printf("usage:filecdc filename\n");
        return -1;
    }
    
    cdc_work(argv[1]);

}