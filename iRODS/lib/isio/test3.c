/* This is a very simple test of the irods standard IO emulation
  library.  When using standard i/o, include stdio.h; to change to
  using isio, change stdio.h to isio.h. */

//#include <stdio.h> 
#include "isio.h"   /* the irods standard IO emulation library */
#include <stdlib.h>
#include <string.h>
main(argc, argv)
int argc;
char **argv;
{
    FILE *FI;
    int i;
    int count, rval, wval;
    char buf[1024*100];
    int bufsize;
    char writeString[]="w1w2w3w4w5w6w7w8w9w0";
    int seekPos;

    int readCount=50;
    int writeCount=20;
 
    if (argc < 2) {
      printf("a.out file [read-count] [write-count] \n");
      exit(-1);
    }

//    FI = fopen(argv[1],"w+");
    FI = fopen(argv[1],"r+");
    if (FI==0)
    {
        fprintf(stderr,"can't open i/o file %s\n",argv[1]);
        exit(-2);
    }

    seekPos=0;
    i = fseek(FI, seekPos, SEEK_SET);
    printf("fseek result: %d\n",i);

    if (argc >= 3) {
       readCount=atoi(argv[2]);
    }

    bufsize = sizeof(buf);

    if (argc >= 4) {
       writeCount = atoi(argv[3]);
    }

    memset(buf, 0, sizeof(buf));
    rval = fread(&buf[0], 1, readCount, FI);
    printf("rval=%d\n",rval);
    if (rval < 0) {
       perror("read stream message");
    }
    if (rval > 0) {
       wval = fwrite(writeString, 1, writeCount, FI);
       printf("wval=%d\n",wval);
       if (wval <= 0) {
	  perror("fwriting data:");
       }
    }
    fclose(FI);
    exit(0);
}
