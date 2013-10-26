#include <stdio.h>
//16655
#include <syscall.h>
#include <string.h>


int
main (int argc, char **argv)
{
 int i;

  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");
  return EXIT_SUCCESS;  

/* create("TEST",512);

 int whandle,rhandle;
 char rBuff[512],wBuff[512];

 memset(rBuff,1,512);
 memset(wBuff,1,512);

 whandle=open("TEST");
 rhandle=open("TEST");
 
 int i=0;
 for(i=0;i<750;i++)
 {
   write(whandle,wBuff,512);
   read(rhandle,rBuff,512);
   if(memcmp(wBuff,rBuff,512)){
    printf("PASS FAILED AT %d",i);
    break;
   }
 }
 return 1;
*/ 
}
