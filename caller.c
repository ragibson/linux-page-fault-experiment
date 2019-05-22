#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "pflog.h" /* used by both kernel module and user program */

void do_syscall(char *call_string); // does the call emulation

// variables shared between main() and the do_syscall() function
int fp;
char the_file[256] = "/sys/kernel/debug/";
char call_buf[MAX_CALL]; /* no call string can be longer */
char resp_buf[100];      /* no response strig can be longer */

void main(int argc, char *argv[]) {
  int fdin;
  unsigned long c = 0;
  char *src;
  struct stat statbuf;
  int i, j;
  int max_idx;
  int rc = 0;

  /* Build the complete file path name and open the file */

  strcat(the_file, dir_name);
  strcat(the_file, "/");
  strcat(the_file, file_name);

  if ((fp = open(the_file, O_RDWR)) == -1) {
    fprintf(stderr, "error opening %s\n", the_file);
    exit(-1);
  }

  /* open the input file */
  if ((fdin = open("BigFile", O_RDONLY)) < 0)
    errx(-1, "can't open BigFile for reading");
  /* find size of input file */
  if (fstat(fdin, &statbuf) < 0)
    errx(-1, "fstat error");
  /* mmap the input file */
  if ((src = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fdin, 0)) ==
      (caddr_t)-1)
    errx(-1, "mmap error for input");

  do_syscall("log_faults");

  /* Random access to locations in the mapped file */
  fprintf(stdout, "Reading %lu bytes from mapped file\n", statbuf.st_size);
  max_idx = statbuf.st_size - 2;
  for (i = 0; i < statbuf.st_size; i++) {
    j = random() % max_idx;
    c += (*(src + j)) >> 2;
  }
  fprintf(stdout, "Read %d bytes, sum %lu\n", i - 1, c);
} /* end main() */

void do_syscall(char *call_string) {
  int rc;

  strcpy(call_buf, call_string);

  rc = write(fp, call_buf, strlen(call_buf) + 1);

  printf("write() returns %d\n", rc);

  rc = read(fp, call_buf, sizeof(call_buf));
}
