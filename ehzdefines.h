#ifndef EHZDEFINES_H
#define EHZDEFINES_H 1


    extern long GetFileSizeByName(char *filename);

    extern volatile int64_t myclock(void);
    extern int mydifftime (int mtime);
//    extern int send_data (struct tm *time_info,int pvetotal, int pvpower, int conscounter, int conspower, double uac, double pvtemp);
    extern int send_data (struct tm *time_info,int pvetotal, int pvpower, int conscounter, int conspower, double uac, double pvtemp, int pv1power,int pvetoday, int pv2_power, int pv2_etoday);

    extern void calculate_power(const char* inp1, const char* inp2, int* vz1, int* vz2, int* ez1, int* ez2);

    // for kbhit emulation

    extern void changemode(int);
    extern int  kbhit(void);
    extern int verbose;
    extern int trace;
    extern int dbgflag;
    extern char debugtime[40];


    #define SECS_IN_DAY (24 * 60 * 60)


    #define MY_CLOCKS_PER_SEC 1000
    // myclock returns msec with MY_CLOCKS_PER_SEC (was usec)


#endif

