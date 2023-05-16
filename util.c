// some utils from emh_zaehler_v2

// sync start of log to multiples of local time
// mtime 60 start at 1 minute, mtime 300 start at 5 minutes

#include <stdio.h>      /* printf */
#include <time.h>       /* time_t, struct tm, difftime, time, mktime */
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint-gcc.h> /* int64_t definitions */

//global verbose flag
int verbose = 0;

//global trace flag
int trace = 0;

//global debug flag
int dbgflag = 0;

// global debugtime
char debugtime[40];


// high res clock in msec
volatile int64_t myclock(void)
{
   struct timespec ts;

   if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
       perror("clock_gettime");
       exit(EXIT_FAILURE);
   }
#if 0
// zum testen statt clock_gettime()
  static struct timespec ts = {2147472,  999999999 }; // 24,855 Tage * 86400, Überlauf beim 12. Aufruf
  ts.tv_sec++;
  printf ("sizeof %d %d %d \n",sizeof(int64_t),sizeof(ts.tv_sec),sizeof(ts.tv_nsec) );
#endif // 0


// wichtig: ERST ts.tv_sec auf int64_t casten, dann mit 1000 multiplizieren,
// sonst gibt es weiterhin Überlauf auf negativen int wert bei *1000 nach 28.5 Tagen
   return ( ((int64_t)ts.tv_sec)*1000+(int64_t)(ts.tv_nsec / 1000000) );

}



int mydifftime (int mtime)
{

    time_t t;
    struct tm *time_info;
    int minsec;
    int waittime;

  // read timedate
    time(&t);
    time_info = localtime( &t );
    minsec=*(&time_info->tm_min) * 60 + *(&time_info->tm_sec);
    waittime = mtime - minsec % mtime;

//    strftime(str, sizeof(str), "%d.%m.%Y %H:%M:%S", time_info);
//    printf("Start Time %s Wait %d secs %d mm %d ss\n", str, waittime, waittime/60, waittime%60);

  return waittime;
}


long GetFileSizeByName(char *filename)
{
	struct stat stat_buf;
	int rc = stat(filename, &stat_buf);
	return rc == 0 ? stat_buf.st_size : -1;
}

