#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
/*
https://pvoutput.org/help/api_specification.html

Service URL
https://pvoutput.org/service/r2/addstatus.jsp

Parameters
This service accepts both GET or POST requests with the following parameters -
Param Field	                Required	Format	    Unit	    Example
d	   Output Date	        Yes	        yyyymmdd    date	    20210228
t	   Time	                Yes	        hh:mm	    time	    14:00
v1	   Energy Generation	YES	        number	    watt hours	10000
v2	   Power Generation	    No	        number	    watts	    2000
v3	   Energy Consumption	No	        number	    watt hours	10000
v4	   Power Consumption	No	        number	    watts	    2000
v5	   Temperature	        No	        decimal	    celsius	    23.4
v6	   Voltage	            No	        decimal	    volts	    239.2
c1	   Cumulative Flag	    YES	        number		            1
n	   Net Flag	            No	        number		            0


Add Batch Status Service
The Add Batch Status service adds up to 30 statuses in a single request.
Service URL
https://pvoutput.org/service/r2/addbatchstatus.jsp

Parameters
This service accepts both GET or POST requests with the following parameters -
Parameter	Field	        Required	Example
data	    Delimited	    Yes	See Data Structure
c1	        Cumulative Flag YES	1

Data Structure
The data parameter consists of up to 30 statuses, each status contains multiple fields.
Field Delimiter	Output Delimiter
        ,               ;

curl -d "data=yyyymmdd,hh:mm,pvetotal,pvpower,conscounter,conspower" -H "X-Pvoutput-Apikey: Your-API-Key" -H "X-Pvoutput-SystemId: Your-System-Id" https://pvoutput.org/service/r2/addbatchstatus.jsp


*/
#include "ehzdefines.h"
#include "pvoutkeys.h"
int send_data (struct tm *time_info,int pvetotal, int pvpower, int conscounter, int conspower, double uac, double pvtemp, int pv1power,int pvetoday, int pv2_power, int pv2_etoday)
//int send_data (struct tm *time_info,int pvetotal, int pvpower, int conscounter, int conspower, double uac, double pvtemp)
{
    char curlstr[2500];
    char cmdstring[2600];
    static int firstrun=1;
    static int errcount=0;
    int filesize;
    FILE *fpcurl;
    char str[40];
    int curlerr = 0;


    // ----- check response of previous curl run curl: (xx) error

    if (!firstrun) {
        if (verbose) printf ("check curl.log\n");
        filesize = GetFileSizeByName("/run/user/1000/curl.log");

        if (filesize > 0) {
            char* sbfbuf;
            char* pac;
            int numread;

            if ((fpcurl = fopen("/run/user/1000/curl.log", "rb")) == NULL) {
                printf("%s Warning, cant open curl.log\n",debugtime);
                curlerr = 1;
            }
            else {
                sbfbuf = malloc(filesize + 1);
                if (sbfbuf == NULL) {
                    printf("malloc error curl.log\n");
                    exit(1);
                }
                *(sbfbuf + filesize) = 0;
                numread = fread(sbfbuf, filesize, 1, fpcurl);
                if (numread < 1) {
                    printf("%s Warning, read error curl.log\n",debugtime);
                    curlerr = 1;
                }
                fclose(fpcurl);
                // curl error?
                pac = strstr(sbfbuf, "curl: (");
                if (pac) {
                printf("%s Warning, Error curl (%d)\n",debugtime, atoi (pac+7));
                   curlerr = 1;
                }
                // HTTP error?
                else {
                    pac = strstr(sbfbuf, "HTTP/1.1 200 OK");
                    if (!pac) {
                        printf("%s Warning, HTTP/1.1 Error Response\n",debugtime );
                        curlerr = 1;
                    }
                    else {
                       curlerr = 0;
                    }
                }
                free(sbfbuf);
            }
        }
        else {
            printf("%s Warning, empty curl.log\n",debugtime);
            curlerr = 1;
        }
    }
    if (curlerr)  {
        errcount++;
        if (errcount > 10) {
        printf ("%s Warning, repeated errors in internet connection\n",debugtime);
        errcount = 0;
        }
    }
    else {
        errcount = 0;
    }
    firstrun = 0;

    remove("/run/user/1000/curl.log");

    // ----- build curl string

    strftime(str, sizeof(str), "%Y%m%d,%H:%M", time_info);
    if (uac > 100.0 && uac < 400.0 && pvtemp > 5.0 && pvtemp < 180.0) {  // add uac and temp if in range
        sprintf(curlstr,"/usr/bin/curl -i -d \"data=%s,%d,%d,%d,%d,%3.1f,%3.1f,%d,%3.1f,%d,%3.1f\" -d \"c1=1\" -H \"X-Pvoutput-Apikey: %s\" -H \"X-Pvoutput-SystemId: %s\" https://pvoutput.org/service/r2/addbatchstatus.jsp",
                str,pvetotal,pvpower,conscounter,conspower,pvtemp,uac,pv1power,(float)pvetoday/1000.0,pv2_power,(float)pv2_etoday/1000.0, APIKEY,SYSTEMID);

 //       sprintf(curlstr,"/usr/bin/curl -i -d \"data=%s,%d,%d,%d,%d,%3.1f,%3.1f\" -d \"c1=1\" -H \"X-Pvoutput-Apikey: %s\" -H \"X-Pvoutput-SystemId: %s\" https://pvoutput.org/service/r2/addbatchstatus.jsp",
 //               str,pvetotal,pvpower,conscounter,conspower,pvtemp,uac, APIKEY,SYSTEMID);
    }
    else {
        sprintf(curlstr,"/usr/bin/curl -i -d \"data=%s,%d,%d,%d,%d\" -d \"c1=1\" -H \"X-Pvoutput-Apikey: %s\" -H \"X-Pvoutput-SystemId: %s\" https://pvoutput.org/service/r2/addbatchstatus.jsp",
                str,pvetotal,pvpower,conscounter,conspower, APIKEY,SYSTEMID);
    }

    // use explicit bash in system command because /bin/sh -> dash leads to error: sh:1 permission denied
    // the curl command is running anyway but i dont like error messages without knowing the exact root cause

    sprintf(cmdstring,"/usr/bin/bash -c \'%s >/run/user/1000/curl.log 2>&1 &\'",curlstr);
    if (verbose)  puts (cmdstring);
    int err;
    err = system(cmdstring);
    if (verbose)  printf ("\ncurl runs in bg status: %d\n",err);

    return 0;

}



