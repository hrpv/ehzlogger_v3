// -------------------------------------------------------------------------------------------------------------
// ehzlogger v3, mqtt
// Werte des emh Zaehlers werten jetzt über das vzlogger Framework im Hintergrund ermittelt und alle 5 minuten
// per mqtt an den mqttbroker mosquitto übermittelt. Das Sampling Timing wird im vzlogger.conf file eingestellt: "aggtime": 300
// ehzlogger startet alle 5 Minuten einen Aufruf von sbfspot, der seine werte ebenfalls per mqtt publiziert
// alternative könnt auch ein cron job alle 5 minuten sbfspot aufrufen, aber das läuft dann asynchron zu den vzlogger Werten
// Der grosse Vorteil mqtt zu verwenden besteht darin, dass auch andere Progeramme  wie evcc zur PV Überschussladung den Zaehlerstand per mqtt ahbolen könenn
// ebenso liefert der modbus Zaehler OR WE517 via mdmd seine Werte per mqtt und auch die Wallbox versteht mqtt.
// so können alle Messwerte zwischen den Anwendunggen geteilt werden.
// Die Werte des EMH Zaehlers und SMA WR werden weiterhin per curl unverändert an den Account Aichwald bei pvoutput.org weitergeschickt
// -------------------------------------------------------------------------------------------------------------------------------------------

//vzlogger/data/chn0/agg { "timestamp": 1636369980000, "value": 38118.691500000001 }
//vzlogger/data/chn1/agg { "timestamp": 1636369980000, "value": 42601.953500000003 }
//sbfspot_2100346262 {"Timestamp": "08/11/2021 18:23:54","SunRise": "08/11/2021 07:21:00","SunSet": "08/11/2021 16:50:00","InvSerial": 2100346262,
//"InvName": "SN: 2100346262","InvTime": "08/11/2021 18:23:53","InvStatus": "Ok","InvTemperature": 0.000,"InvGridRelay": "Information not available",
//"EToday": 15.175,"ETotal": 59975.397,"PACTot": 0.000,"UDC1": 0.000,"UDC2": 0.000,"IDC1": 0.000,"IDC2": 0.000,
//"PDC1": 0.000,"PDC2": 0.000,"UAC1": 0.000,"UAC2": 0.000,"UAC3": 0.000}


//24.11.21 mqtt publish des zweirichtung ehzzaehlers dazu gefügt
//ehzmeter/power in watt. negative werte sind einspeisung positive verbrauch
//im prinzip leistet die volkszaehler middleware das gleiche,
//aber muss dann auch extra dazu installiert werden (einiges an unnötigem overhead)

// 25.11.21 log zeiten von 5 minuten auf 1 Minute runtergesetzt aber es wird wird nur alle 5 minuten
// geloggt und werte an pvoutput.org übertragen. Evt. Mittelung der Werte vor upload tbd
// Min. Auflösung 0.0001kwh: 6W bei 60s Messzeit
// 0.0001kwh * 1000 * 3600 = 360 Ws/60s = 6W

// 13.02.2022 Korrektur für verbrauchte Leistung hinzugefügt:
// Bei stark schwankender PV Leistung (pvpower) liefert die Formel für den Verbrauch
// conspower = (pvpower + gridpower) gelegentlich negative Verbrauchswerte was natürlich physikalisch Unsinnn ist.
// gridpower = -epower + vpower negativ bei einspeisung positiv bei Verbrauch
// Da die Werte ja nur alle Minute ermittelt werden und trotzdem einigermassen flüssig mitlaufen sollen wurde jetzt folgender "Trial and Error Algorithmus aus excel" für die conspower gewählt:
// pvpower wird aus pvetotal berechnet als pvpower_avg, die kleinste Einheit sind dann zwar 60 Watt bei 1 min. Sampling rate aber pvpower_avg sprint nicht extrem so wie pvpower
// conspower_raw = (pvpower_avg + gridpower)
// Mittelung von conspower_raw Tiefpass 1. Ordnung Konstante 0.1 (scale 10: conspower_filt = conspower_filt + conspower_raw - conspower_filt/10)
// Dann Korrektur von pvpower und conspower
// pvpower_corr = 0;
// if (pvpower > 0) {
//     if (pvpower_avg  + gridpower <  MINCONS)  // Verbrauch < MINCONS (MINCONS=100 W)
//         pvpower_corr = -gridpower + max(MINCONS, conspower_filt/10);   // Korrektur verschönern, mind. Verbrauch 100W oder letzter Mittelwert
//     else
//         pvpower_corr = pvpower_avg;
// }
// conspower = pvpower_corr + gridpower;
//
// publish an evcc: pvpower_corr, gridpower
//
// pvoutput bekommt weiterhin die unveränderte pvpower, wegen Optik und Sunny Explorer vergleichbarkeit,
// aber eine koorigierte conspower
// gelegentliche kleinere conscounter werte sind unkritisch es wird einfach nochmal der alte Wert ausgegeben,
// solange bis der neue wieder grössser ist.



#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint-gcc.h> /* int64_t definitions */


#include <mosquitto.h>
#include "ehzdefines.h"

#define mqtt_host "127.0.0.1"   // use localhost, unsecured, no tls
#define mqtt_port 8889
#define MQTT_TOPIC_CHN0 "vzlogger/data/chn0/agg"   // EHZ D0 1.8.1 Verbrauch
#define MQTT_TOPIC_CHN1 "vzlogger/data/chn1/agg"   // EHZ D0 2.8.1 Einspeisung
#define MQTT_TOPIC_WR   "sbfspot_2100346262"       // SMA Wr mit Seriennummer

#define EHZTOPIC_GRIDPWR "ehzmeter/power"          // gridpower für evcc
#define EHZTOPIC_PVPWR "ehzmeter/pvpower"          // pvpower fuer evcc

#define MINCONS 100    //minimum 100W Power consumption


static char *ptopics[] = {MQTT_TOPIC_CHN0,MQTT_TOPIC_CHN1,MQTT_TOPIC_WR};
// static int numtopics=3;

static volatile int run = 1;       // to check if volatile is really needed

// flag > 0 new mqtt message received
static volatile int chan0_flag=0;
static volatile int chan1_flag=0;
static volatile int wr_flag=0;

static char wr_buf[1024];
static char ch0_buf[512];
static char ch1_buf[512];


// signal handler for for CTL C and terminate process
// note, this is called only once, else you need to activate the signal again inside the signal handler
// https://stackoverflow.com/questions/17728050/second-signal-call-in-sighandler-what-for

void handle_signal(int s)
{
    run = 0;
    puts("Process will terminate\n");
}

/* Callback called when the client knows to the best of its abilities that a
 * PUBLISH has been successfully sent. For QoS 0 this means the message has
 * been completely written to the operating system. For QoS 1 this means we
 * have received a PUBACK from the broker. For QoS 2 this means we have
 * received a PUBCOMP from the broker. */
void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	if (verbose) printf("Message with mid %d has been published.\n", mid);
}


/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
	int rc;
	/* Print out the connection result. mosquitto_connack_string() produces an
	 * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
	 * clients is mosquitto_reason_string().
	 */
	printf("on_connect: %s\n", mosquitto_connack_string(reason_code));
	if(reason_code != 0){
		/* If the connection fails for any reason, we don't want to keep on
		 * retrying in this example, so disconnect. Without this, the client
		 * will attempt to reconnect. */
		mosquitto_disconnect(mosq);
	}

	/* Making subscriptions in the on_connect() callback means that if the
	 * connection drops and is automatically resumed by the client, then the
	 * subscriptions will be recreated when the client reconnects. */
  //  rc= mosquitto_subscribe_multiple(mosq, NULL, numtopics, ptopics, 1, 0, NULL);  // subscribe to a lost of topics
    rc=mosquitto_subscribe(mosq,NULL,ptopics[0],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[1],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[2],1);
	if(rc != MOSQ_ERR_SUCCESS){
		fprintf(stderr, "Error subscribing: %s\n", mosquitto_strerror(rc));
		/* We might as well disconnect if we were unable to subscribe */
		mosquitto_disconnect(mosq);
	}

}

/* Callback called when the broker sends a SUBACK in response to a SUBSCRIBE. */
void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	bool have_subscription = false;

	/* In this example we only subscribe to a single topic at once, but a
	 * SUBSCRIBE can contain many topics at once, so this is one way to check
	 * them all. */
	for(i=0; i<qos_count; i++){
		printf("on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
		if(granted_qos[i] <= 2){
			have_subscription = true;
		}
	}
	if(have_subscription == false){
		/* The broker rejected all of our subscriptions, we know we only sent
		 * the one SUBSCRIBE, so there is no point remaining connected. */
		fprintf(stderr, "Error: All subscriptions rejected.\n");
		mosquitto_disconnect(mosq);
	}
}

/* Callback called when the client receives a message. */
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	bool match = false;
	/* This blindly prints the payload, but the payload can be anything so take care. */
    if (verbose) printf("%s %d %s\n", msg->topic, msg->qos, (char *)msg->payload);

	mosquitto_topic_matches_sub(MQTT_TOPIC_CHN0, msg->topic, &match);
	if (match) {
	    if (verbose) printf("chan0 received \n");
        chan0_flag=1;
        strncpy(ch0_buf,msg->payload, sizeof(ch0_buf)-1);
	}
	else {
        mosquitto_topic_matches_sub(MQTT_TOPIC_CHN1, msg->topic, &match);
        if (match) {
            if (verbose) printf("chan1 received \n");
            chan1_flag=1;
            strncpy(ch1_buf,msg->payload, sizeof(ch1_buf)-1);
        }
        else {
            mosquitto_topic_matches_sub(MQTT_TOPIC_WR, msg->topic, &match);
            if (match) {
                if (verbose) printf("wr received \n");
                wr_flag=1;
                strncpy(wr_buf,msg->payload, sizeof(wr_buf)-1);
            }
        }
	}

}



/* This function publish the qridcounter power or corrected pvpower  in watts:*/
void publish_power(struct mosquitto* mosq, char* topic, int power)
{
    char payload[20];
    int rc;

    /* Print it to a string for easy human reading - payload format is highly
     * application dependent. */
    snprintf(payload, sizeof(payload), "%d", power);

    /* Publish the message
     * mosq - our client instance
     * *mid = NULL - we don't want to know what the message id for this message is
     * topic = "example/temperature" - the topic on which this message will be published
     * payloadlen = strlen(payload) - the length of our payload in bytes
     * payload - the actual payload
     * qos = 2 - publish with QoS 2 for this example
     * retain = false - do not use the retained message feature for this message
     */
    rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 2, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
    }
}





// get timestamp from vzlogger EPOCH Secs * 1000 (ms) and ctr value in float
//vzlogger/data/chn0/agg 0 { "timestamp": 1636901760000, "value": 38118.691500000001 }
//vzlogger/data/chn1/agg 0 { "timestamp": 1636901760000, "value": 42601.953500000003 }


void get_time_stamp_cntr(char *buf, int64_t *time, int *cntr)
{
    char *pt;
    pt = strstr(buf,"\"timestamp\":");
    if (pt != NULL) {

        pt+=strlen("\"timestamp\":");
        *time = atoll(pt);
    }
    else {
        *time = -1;
    }
    pt = strstr(buf,"\"value\":");
    if (pt != NULL) {

        pt+=strlen("\"value\":");
        *cntr = (atof(pt) * 10000);  //convert to integer, 4 decimal places
    }
    else {
        *cntr = -1;
    }
}


void get_wr_values (char *sbfbuf, int *pvpower, int *pvetotal, double *uac, double *pvtemp, char *debugtime)
{
    long filesize;
    char* pac;

    filesize = GetFileSizeByName("/run/user/1000/sbfspot.log");  // check only existence of log, other values parse from mqtt
    if (filesize > 0) {
        pac = strstr(sbfbuf, "\"PACTot\":");
        if (pac) {
            *pvpower = atof(pac+strlen("\"PACTot\":"));  // convert to int watts
        }
        else {
            printf("%s Warning, no pvpower from SBFSpot\n",debugtime);
            *pvpower = 0;
        }
        pac = strstr(sbfbuf, "\"ETotal\":");
        if (pac) {
            *pvetotal = 1000 * atof(pac+strlen("\"ETotal\":")); // convert to int wh
        }
        else {
            printf("%s Warning, no pvetotal from SBFSpot\n",debugtime);
            *pvetotal= 0;
        }
        pac  = strstr(sbfbuf, "\"UAC1\":");   // works only so easy if Uac is on phase 1
        if (pac) {
            *uac = atof(pac+strlen("\"UAC1\":"));
        }
        else {
            printf("%s Warning, no Uac from SBFSpot\n",debugtime);
            *uac = 0.0;
        }
        pac = strstr(sbfbuf, "\"InvTemperature\":");
        if (pac) {
            *pvtemp = atof(pac+strlen("\"InvTemperature\":"));
        }
        else {
            printf("%s Warning, no Device Temperature from SBFSpot\n",debugtime);
            *pvtemp = 0.0;
        }
    }
    else {
        printf("%s Warning, no output from SBFSpot\n",debugtime);
        *pvpower = 0;
        *pvetotal= 0;
        *pvtemp = 0.0;
        *uac=0;
    }
    *pvetotal *= 10;     // units of other counter values are 0.1 wh
}

/**
 * Find maximum between two numbers.
 */
int max(int num1, int num2)
{
    return (num1 > num2 ) ? num1 : num2;
}


//--------------------------------------------------------------
// main
//
// argv[1] logfilenane  opt. argv[2] verbose
// sampling time is fixed by vzlogger config. default 300 s
//---------------------------------------------------------------



int main(int argc, char **argv)
{

    int start=1;                            // wait for first ehz messages

    char ch0_0_buf[512];   // first 1.8.1 (timestamp in ms included)
    char ch1_0_buf[512];   // first 2.8.1 (timestamp in ms included)


    int timecntr = 0;      // log if 5x60s vorbei sind

	int64_t time1, time2;
	double difftime;
	int epower;
	int vpower;
	// kwh * 10000
	int vz1, vz2, ez1, ez2;

	time_t t;
	struct tm *time_info;
	char str[80];
	char outline[256];

	FILE *fp;
	int64_t mtime = 300/5;  // default sampling time in seconds, 1 minutes

	char filename[80];
    int pvpower;
    int pvetotal;  //kwh * 10000


    int firstloop=1;

    int apvpower[5] = {0};
    int aepower[5] = {0};
    int avpower[5] = {0};
    int conspower;
    int conscounter;
    int conscounterprev = 0;
    double uac, pvtemp;   // not stored, only sent if pv is active (values > 0)

    int apve[2] = {0};  // pvecounter backup
    int pvecount = 0;

    int sbfstarted = 0;
    int pvpower_avg = 0;  //average pvpower built from pvetotal [W]
    int gridpower =0;    // positiv consumption, negative export
    int pvpower_corr = 0; // corrected pv power
                             // average conspower lowpass constsnt 0.1
    int conspower_filt = 0; // power in 0.1 W Integermath: conspower_filt * 10
    int conspower_raw = 0; // uncorrected conspower

	// --- copy args
	if (argc < 2) {
        printf ("Usage: ehz_logger_v3 --%s;%s-- logfile [verbose|trace]\n", __DATE__, __TIME__);
        exit(1);
	}
	strcpy(filename, argv[1]);
	if (argc >= 3) {
		// opt. get verbose flag
        if (!strncasecmp(argv[2], "v", 1))  {
            verbose = 1;
        }
        if (!strncasecmp(argv[2], "t", 1))  {
            verbose = 1;
            trace = 1;
        }
	}

    if (verbose) setvbuf(stdout, NULL, _IONBF, 0);

	printf("\n------- Start EHZ_Logger V3 %s;%s ---------\n", __DATE__, __TIME__);
    fflush(stdout); // force output even when buffered

    struct mosquitto *mosq;
    int rc = 0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Required before calling other mosquitto functions */
    mosquitto_lib_init();
	/* Create a new client instance.
	 * id = NULL -> ask the broker to generate a client id for us
	 * clean session = true -> the broker should remove old sessions when we connect
	 * obj = NULL -> we aren't passing any of our private data for callbacks
	 */
    mosq = mosquitto_new(NULL, true, NULL);
	if(mosq == NULL){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
    }

	/* Configure callbacks. This should be done before connecting ideally. */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_message_callback_set(mosq, on_message);

	mosquitto_publish_callback_set(mosq, on_publish);

	/* Connect to test.mosquitto.org on port 1883, with a keepalive of 60 seconds.
	 * This call makes the socket connection only, it does not complete the MQTT
	 * CONNECT/CONNACK flow, you should use mosquitto_loop_start() or
	 * mosquitto_loop_forever() for processing net traffic. */
	 /* localhost or 127.0.0.1 is both o.k. */
	rc = mosquitto_connect(mosq, "127.0.0.1", 1883, 60);
	if(rc != MOSQ_ERR_SUCCESS){
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}

    fflush(stdout); // force output even when buffered

    while(run){
        fflush(stdout); // force output even when buffered
            // non blocking loop
        rc = mosquitto_loop(mosq, -1, 1);   // standard timeout is 1 sec
        if(run && rc){  // any error
            printf("connection error!\n");
            sleep(10);
            mosquitto_reconnect(mosq);
        }
        else if (run) {   // ----- handle the messages -----
           if (start) {
               if (chan0_flag && chan1_flag) {    // both channels received
                    chan0_flag = 0;
                    chan1_flag = 0;
                   // read timedate, for debug
                    time(&t);
                    time_info = localtime( &t );
                    strftime(debugtime, sizeof(debugtime), "%Y%m%d,%H:%M:%S", time_info);
                    printf("Start, first ch0 and ch1 message received %s\n", debugtime);
                    memcpy(ch0_0_buf,ch0_buf,sizeof (ch0_0_buf));
                    memcpy(ch1_0_buf,ch1_buf,sizeof (ch1_0_buf));

                    get_time_stamp_cntr(ch0_0_buf, &time1, &vz1); // get timestamp and vz counter 1.8.1
                    get_time_stamp_cntr(ch1_0_buf, &time2, &ez1);  // get timestamp and ez counter 2.8.1

                    if (time1 == -1 || time2 == -1 || vz1 == -1 || ez1 == -1) {
                        printf ("Start, severe error, exit %s\n", debugtime);
                        exit(1);
                    }

                    // read timedate, for debug from vzlogger timestamp
                    time_t tsec=time1/1000;
                    time_info = localtime( &tsec );
                    strftime(debugtime, sizeof(debugtime), "%Y%m%d,%H:%M:%S", time_info);
                    if (time1 != time2) {
                        printf("Start, error different timestamps ch0 and ch1, retry %s\n", debugtime);
                    }
                    else {
                        start=0;   // start sucessfull

                    }

                }
           }
           else {  // endless loop after successfull start
                if (!sbfstarted) {
                    sbfstarted=1;
                    //get current pv values from sbfspot in the background, once per ehz sampling interval
                    // clear sbspot.log
                    if (verbose) printf("remove old sbfspot.log\n");
                    remove("/run/user/1000/sbfspot.log");
                    if (verbose) printf("start sbfspot\n");
                    int err;
                    err = system("/usr/local/bin/sbfspot.3/SBFspot  -v -finq -nocsv -nosql -mqtt >/run/user/1000/sbfspot.log 2>&1 &");
                    if (verbose) printf("sbfspot runs in the background, status: %d\n",err);

                }
                // wait for next values of ch0 and ch1 and spfspot
                if (chan0_flag && chan1_flag) {    // both channels received
                    chan0_flag = 0;
                    chan1_flag = 0;
                   // read timedate, for debug
                    time(&t);
                    time_info = localtime( &t );

                    strftime(debugtime, sizeof(debugtime), "%Y%m%d,%H:%M:%S", time_info);
                    if (verbose) printf("next ch0 and ch1 message received %s\n", debugtime);
                    memcpy(ch0_0_buf,ch0_buf,sizeof (ch0_0_buf));
                    memcpy(ch1_0_buf,ch1_buf,sizeof (ch1_0_buf));

                    get_time_stamp_cntr(ch0_0_buf, &time2, &vz2); //  get timestamp and vz counter 1.8.1
                    get_time_stamp_cntr(ch1_0_buf, &time2, &ez2);  // get timestamp and ez counter 2.8.1

                    if (time2 == -1 || vz2 == -1 || ez2 == -1) {
                        printf ("Warning, invalid EHZ data, skip values %s\n", debugtime);
                        difftime = (double)mtime;  // set to default
                    }
                    else {
                            difftime = (double)(time2 - time1) / MY_CLOCKS_PER_SEC; // use timestamp from vzlogger
                    }
                    if (verbose) printf("Difftime %4.3f\n", difftime);

                    if (wr_flag) {  // sbfspot data over mqtt received
                        wr_flag = 0;
                        get_wr_values (wr_buf, &pvpower, &pvetotal, &uac, &pvtemp, debugtime);
                    }
                    else {
                        printf ("Warning, no wr data received, skip values %s\n", debugtime);
                        pvpower = 0;
                        pvetotal = 0;
                        uac = 0.0;
                        pvtemp = 0.0;
                    }
                    sbfstarted=0;  // activate sbfspot call again

                    if (pvetotal > 0) {   // store pvetotal as backup in case of pvetotal error
                        apve[0] = apve[1];
                        apve[1] = pvetotal;
                        if (pvecount < 2) {
                            pvecount++;
                            if (verbose) printf("collect pvetotal last values\n");
                         }
                    }
                    else { // if pvetotal= 0
                        if (pvecount >= 2) {
                            int pvewatts = ((apve[1]-apve[0])*360)/difftime;
                            pvpower_avg = pvewatts;
                            if (pvewatts >= 0 && pvewatts <= 20000) {   // in range
                                pvetotal= apve[1];  // use last value
                                printf("%s Warning, no pvetotal from SBFSpot, use last value\n",debugtime);
                            }
                            else {
                                pvpower_avg = 0;
                                pvecount = 0;  // invalid backup, set counter to zero
                                apve[0] = 0;
                                apve[1] = 0;
                                printf("%s Warning, no pvetotal from SBFSpot, pvetotal set zero\n",debugtime);
                             }
                        }
                    }

                    // ------ calculate all powers -----------
                    if (verbose) printf("kwh VZ %06.4f %06.4f EZ %06.4f %06.4f PV %06.4f\n",
                        ((double)vz1) / 10000, ((double)vz2) / 10000, ((double)ez1) / 10000, ((double)ez2) / 10000, ((double)pvetotal)/10000);

                    if (pvecount >= 2)
                        pvpower_avg  = ((apve[1]-apve[0])*360)/difftime;
                    else
                        pvpower_avg  = pvpower;  // first runs


                    vpower = (double)((vz2 - vz1) * 360) / difftime;
                    epower = (double)((ez2 - ez1) * 360) / difftime;
                    gridpower = vpower - epower;



                    //Gesamtverbrauch (unit Watt):
                    // PVErzeugung - Einspeisung + Netzbezug

                    conspower_raw = pvpower_avg + gridpower;

                    if (pvecount >= 2)
                        conspower_filt = conspower_filt + conspower_raw - conspower_filt/10;
                    else
                        conspower_filt = 10 * conspower_raw;  // first runs


//                  correction of pvpower to avoid negative consumption
                    pvpower_corr = 0;
                    if (pvpower > 0) {
                        if (pvpower_avg  + gridpower <  MINCONS) {  // Verbrauch < MINCONS (MINCONS=100 W)
                            pvpower_corr = -gridpower + max(MINCONS, conspower_filt/10);   // Korrektur verschönern, mind. Verbrauch 100W oder letzter Mittelwert
                            printf ("Warning, corrected pvpower: %s old: %d new: %d \n", debugtime,pvpower_avg,pvpower_corr);
                        }
                        else
                            pvpower_corr = pvpower_avg;
                    }
                    conspower = pvpower_corr + gridpower;

                   // publish qrid watts for evcc
                    publish_power(mosq, EHZTOPIC_GRIDPWR, gridpower);
                   // publish pvpowercorr watts for evcc
                    publish_power(mosq, EHZTOPIC_PVPWR,  pvpower_corr);

                // debug: put pvpower[w], pvetotal, vz, ez to separate logfile every minute unit 0.1wh
                    if (trace) {
                        static int fileheader=1;

                        FILE *fpm;
                        time(&t);
                        time_info = localtime( &t );
                        strftime(str, sizeof(str), "%Y%m%d %H:%M", time_info);
                        if (fileheader) {
                            fileheader = 0;
                             sprintf(outline,"%s\n",
                             "time,pvetotal,pvpower,vz2,ez2,gridpower,conspower,conspower_raw,conspower_filt,pvpower_avg,pvpower_corr");
                        }
                        else {
                            sprintf(outline,"%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                              str,pvetotal,pvpower,vz2,ez2,gridpower,conspower,conspower_raw,conspower_filt/10,pvpower_avg,pvpower_corr);
                        }

                        if ((fpm = fopen("/var/log/ehz_logger_v2/minute.log", "a+")) == NULL) {
                            printf("%s Error cant create or open logfile %s\n", debugtime, argv[2]);
                            exit(1);
                        }
                        fputs(outline, fpm);
                        fclose(fpm);
                    }


                    if (conspower < 0) {   // should never happen again!
                       printf("%s Warning: neg. consumption, correction\n",debugtime);
                       conspower = (apvpower[2]-aepower[2]-avpower[2]+ apvpower[0]-aepower[0]-avpower[0])/2;
                    }
                    // Werte für Mittelwertkorrektur speichern (alles integer)
                    apvpower[0]=apvpower[1]; apvpower[1]=apvpower[2]; apvpower[2]=pvpower;
                    aepower[0]=aepower[1]; aepower[1]=aepower[2]; aepower[2]=epower;
                    avpower[0]=avpower[1]; avpower[1]=avpower[2]; avpower[2]=vpower;

                    // Gesamtzaehlerstand Verbrauch (unit Wh):
                    //PVZaehler-Einspeisezaehler+Bezugszaehler
                    //wenn Gesamtzaehlerstand < Gesamtzaehlerstand_Previous,
                    //dann nutze PreviousZahlerstnd
                    // beim ersten durchlauf könnte ein initialer offset gesetzt werden
                    // damit der initiale Wert vz2 (virtueller Differenz Zaehler) ist

                    if (firstloop) {
                        conscounterprev = pvetotal - ez2 + vz2;
                        firstloop = 0;
                    }
                    conscounter = pvetotal - ez2 + vz2;
                    if (conscounter < conscounterprev)
                    {
                       printf("%s Warning: neg. conscounter, correction\n",debugtime);
                       conscounter=conscounterprev;
                    }
                    conscounterprev=conscounter;

                    if (verbose) printf("vpower epower pvpower pvetotal %06d %06d %06d %06d\n", vpower, epower, pvpower, pvetotal);
                    if (verbose) printf("conspower conscounter %08d %08d\n",conspower,conscounter/10);

                    // ---- copy values for next loop
                    time1=time2;
                    ez1=ez2;
                    vz1=vz2;
                    // read timedate
                    time(&t);
                    time_info = localtime( &t );
                    strftime(str, sizeof(str), "%Y%m%d,%H:%M", time_info);

                      // set timedate, for debug
                    strftime(debugtime, sizeof(str), "%Y%m%d,%H:%M:%S", time_info);

                    // format for logfile, optimize fields for curl for delayed or repeated upload if necessary

                    sprintf(outline,"%s,%d,%d,%d,%d;%d,%d,%d,%d\n",
                        str,(int)pvetotal/10,(int)pvpower,conscounter/10, conspower,vz2,ez2,vpower,epower);

                    if (verbose) puts(outline);


                    // rough sanity check of values:
                    int invalid = 0;
                    if (    pvpower > 10000 || pvpower < 0
                        || conspower > 50000 || conspower < 0
                        || vpower > 50000 || vpower < 0
                        || epower > 10000 || epower < 0 ) {
                            printf ("%s Warning skip out of range values, no storage \n",debugtime);
                            printf ("%s Warning %s",debugtime, outline);
                            invalid = 1;
                    }
                    timecntr++;

                    if (!invalid && timecntr >= 5) {   // log if 5x60s vorbei sind
                        timecntr = 0;
                        // write to logfile
                        if (verbose) puts("Write to Logfile\n");
                        if ((fp = fopen(filename, "a+")) == NULL) {
                            printf("%s Error cant create or open logfile %s\n", debugtime, argv[2]);
                            exit(1);
                        }
                        fputs(outline, fp);
                        fclose(fp);

                        // send curl string for pvoutput.org

                        send_data (time_info,pvetotal/10, pvpower,conscounter/10 , conspower, uac, pvtemp);

                    }
                }  // chan0_flag && chan1_flag
            }  // endless loop after start
       } // else if (run)
    }  // while(run)

    mosquitto_lib_cleanup();


    return rc;
}
