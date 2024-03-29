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

// 16.02.2024
// Balkonkraftwerk (112484880479) auf L2 dazu addiert

// 17.10.2023
// sma + hoymiles leistung pro phase übertragen, damit die victron anzeige ac load stimmiger ist.
// korrektur rechnung für pv leitung deaktiviert, bei negativem hausverbrauch leistung auf null gesetzt.
// rechnung ist eh schwieriger geworden, da victron auf null regelt.
// langfristig kann man den log nach pvoutput.org canceln
//
//

// 15.02.2032:
// Hoymiles WR dazu pv2_xxx
// solar/ac/power 39.0          W  -> int pv2_power
// solar/ac/yieldtotal 439.916  Kwh -> x10000 für 0.1 wh  int pv2_etotal
// solar/ac/yieldday 23253      Wh  -> x10 für 0.1 wh     int pv2_today
// solar/ac/is_valid 1                                    int pv2_valid

// die Werte für Einspeisung und Verbrauch und aktuelle Leistung werden künftig von
// von dem EM24 Ethernet Zähler geliefert

// mbmd/cgex3x02-1/Power  Grid Power float in Watt. plus Bezug negativ Lieferung
// mbmd/cgex3x02-1/Import float Bezug in kwh
// mbmd/cgex3x02-1/Export float Lieferung in kwh






// Zaehler Z2 ist derzeit noch ohne Pin,liefert Werte in Wh, wird aber ohnehin nur einmal täglich kurz vor Mitternacht erfasst:
//vzlogger/data/chn0/agg { "timestamp": 1683731700000, "value": 3000 }
//vzlogger/data/chn1/agg { "timestamp": 1683731700000, "value": 16000 }

// Zaehler Z1 wird nicht mehr erfasst, der sollte in etwa die gleichen Werte übers Jahr haben wie der EM24 Zaehler
//vzlogger/data/chn0/agg { "timestamp": 1636369980000, "value": 38118.691500000001 }
//vzlogger/data/chn1/agg { "timestamp": 1636369980000, "value": 42601.953500000003 }

// SMA Zaehler einlesen wie gehabt.
// eToday und Etotal für Mitternachtserfassung
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

// unsused: gridpower is now "mbmd/cgex3x02-1/Power"
// #define EHZTOPIC_GRIDPWR "ehzmeter/power"       // gridpower für evcc

#define EHZTOPIC_PVPWR "ehzmeter/pvpower"          // pvpower fuer evcc und victron
#define EHZTOPIC_PVTODAY "ehzmeter/pvtoday"        // pv energy today in 0.1wh (int)
#define EHZTOPIC_PVTOTAL "ehzmeter/pvtotal"        // pv energy total in 0.1wh (int)
#define EHZTOPIC_PVPWRL123 "ehzmeter/pvpwrl123"    // 3 CSV-Values L1,L2,L3 pvpower+pv2_l1_pwr,pv2_l2_pwr,pv3_l3_pwr

#define MBMD_TOPIC_PWR "mbmd/cgex3x02-1/Power"     // float in Watt. plus Bezug negativ Lieferung
#define MBMD_TOPIC_IMP "mbmd/cgex3x02-1/Import"    // float Bezug in kwh
#define MBMD_TOPIC_EXP "mbmd/cgex3x02-1/Export"    // float Lieferung in kwh

#define HMS_TOPIC_PWR "solar/ac/power"             // float W  -> int pv2_power
#define HMS_TOPIC_TOT "solar/ac/yieldtotal"        // float Kwh -> x10000 für 0.1 wh  int pv2_etotal
#define HMS_TOPIC_DAY "solar/ac/yieldday"          // float Wh  -> x10 für 0.1 wh     int pv2_today
#define HMS_TOPIC_FLG "solar/ac/is_valid"          // int flag                          int pv2_valid

#define HMS_TOPIC_L1_PWR "solar/116480421861/0/power"
#define HMS_TOPIC_L2_PWR "solar/116480156369/0/power"
#define HMS_TOPIC_L3_PWR "solar/116480424201/0/power"

#define HMS_TOPIC_L2_B_PWR "solar/112484880479/0/power"    // balkonkraftwerk


#define MINCONS 0    //minimum  Power consumption was 100W, set to 0 with Victron Battery


static char *ptopics[] = {MQTT_TOPIC_CHN0,MQTT_TOPIC_CHN1,MQTT_TOPIC_WR,MBMD_TOPIC_PWR,
    MBMD_TOPIC_IMP,MBMD_TOPIC_EXP,HMS_TOPIC_PWR,HMS_TOPIC_TOT,HMS_TOPIC_DAY,HMS_TOPIC_FLG,
    HMS_TOPIC_L1_PWR,HMS_TOPIC_L2_PWR,HMS_TOPIC_L3_PWR,HMS_TOPIC_L2_B_PWR};
// static int numtopics=3;

static volatile int run = 1;       // to check if volatile is really needed

// flag > 0 new mqtt message received
static volatile int chan0_flag=0;
static volatile int chan1_flag=0;
static volatile int wr_flag=0;
static volatile int mb_pwr_flag=0;
static volatile int mb_imp_flag=0;
static volatile int mb_exp_flag=0;


static char wr_buf[1024];
static char ch0_buf[512];
static char ch1_buf[512];

static volatile int mb_pwr=0;  // int power in watt
static volatile int mb_imp=0;  // int import energy in 0.1 wh
static volatile int mb_exp=0;  // int export energy in 0.1 wh

static volatile int pv2_power=0;   // pv2 power in watt
static volatile int pv2_etotal=0;  // pv2 total energy in 0.1 wh
static volatile int pv2_etoday=0;  // pv2 today energy in 0.1 wh
static volatile int pv2_flag=0;    // pv2 ac valid flag

static volatile int pv2_l1_pwr=0;   // split power to make victron happy
static volatile int pv2_l2_pwr=0;
static volatile int pv2_l3_pwr=0;
static volatile int pv2_l2_b_pwr=0;

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
  //  rc= mosquitto_subscribe_multiple(mosq, NULL, numtopics, ptopics, 1, 0, NULL);  // subscribe to a lots of topics

    rc= mosquitto_subscribe(mosq,NULL,ptopics[0],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[1],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[2],1);

    rc+=mosquitto_subscribe(mosq,NULL,ptopics[3],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[4],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[5],1);

    rc+=mosquitto_subscribe(mosq,NULL,ptopics[6],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[7],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[8],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[9],1);

    rc+=mosquitto_subscribe(mosq,NULL,ptopics[10],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[11],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[12],1);
    rc+=mosquitto_subscribe(mosq,NULL,ptopics[13],1);

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
    if (dbgflag) printf("%s %d %s\n", msg->topic, msg->qos, (char *)msg->payload);

    do {
        mosquitto_topic_matches_sub(MQTT_TOPIC_CHN0, msg->topic, &match);
        if (match) {
            if (dbgflag) printf("chan0 received \n");
            chan0_flag=1;
            strncpy(ch0_buf,msg->payload, sizeof(ch0_buf)-1);
            break;
        }
        mosquitto_topic_matches_sub(MQTT_TOPIC_CHN1, msg->topic, &match);
        if (match) {
            if (dbgflag) printf("chan1 received \n");
            chan1_flag=1;
            strncpy(ch1_buf,msg->payload, sizeof(ch1_buf)-1);
            break;
        }
        mosquitto_topic_matches_sub(MQTT_TOPIC_WR, msg->topic, &match);
        if (match) {
            if (dbgflag) printf("wr received \n");
            wr_flag=1;
            strncpy(wr_buf,msg->payload, sizeof(wr_buf)-1);
            break;
        }
        mosquitto_topic_matches_sub(MBMD_TOPIC_PWR, msg->topic, &match);
        if (match) {
            mb_pwr_flag=1;
            mb_pwr = atof(msg->payload);
            if (dbgflag) printf("mbmd gridpwr received %d\n",mb_pwr);
            break;
        }
        mosquitto_topic_matches_sub(MBMD_TOPIC_IMP, msg->topic, &match);
        if (match) {
            mb_imp_flag=1;
            mb_imp = 10000 * atof(msg->payload);
            if (dbgflag) printf("mbmd import received %d\n", mb_imp);
            break;
        }
        mosquitto_topic_matches_sub(MBMD_TOPIC_EXP, msg->topic, &match);
        if (match) {
            mb_exp_flag=1;
            mb_exp = 10000 * atof(msg->payload);
            if (dbgflag) printf("mbmd export received %d\n", mb_exp);
            break;
        }

        mosquitto_topic_matches_sub(HMS_TOPIC_PWR, msg->topic, &match);
        if (match) {
            pv2_power = atof(msg->payload);
            if (dbgflag) printf("pv2 power received %d\n", pv2_power);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_TOT, msg->topic, &match);
        if (match) {
            pv2_etotal = 10000 * atof(msg->payload);
            if (dbgflag) printf("pv2 etotal received %d\n", pv2_etotal);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_DAY, msg->topic, &match);
        if (match) {
            pv2_etoday = 10 * atof(msg->payload);
            if (dbgflag) printf("pv2 etoday received %d\n", pv2_etoday);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_FLG, msg->topic, &match);
        if (match) {
            pv2_flag = atoi(msg->payload);
            if (dbgflag) printf("pv2 ac valid received %d\n", pv2_flag);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_L1_PWR, msg->topic, &match);
        if (match) {
            pv2_l1_pwr = atof(msg->payload);
            if (dbgflag) printf("pv2 l1 pwr received %d\n", pv2_flag);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_L2_PWR, msg->topic, &match);
        if (match) {
            pv2_l2_pwr= atof(msg->payload);
            if (dbgflag) printf("pv2 l2 pwr received %d\n", pv2_flag);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_L2_B_PWR, msg->topic, &match);
        if (match) {
            pv2_l2_b_pwr= atof(msg->payload);
            if (dbgflag) printf("pv2 l2 balkon pwr received %d\n", pv2_flag);
            break;
        }
        mosquitto_topic_matches_sub(HMS_TOPIC_L3_PWR, msg->topic, &match);
        if (match) {
            pv2_l3_pwr = atof(msg->payload);
            if (dbgflag) printf("pv2 l3 pwr received %d\n", pv2_flag);
            break;
        }


    } while (0);

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

/* This function publish the pv power per phase in int watts:*/
void publish_powerl123(struct mosquitto* mosq, char* topic, int pwrl1,int pwrl2,int pwrl3)
{
    char payload[40];
    int rc;

    /* Print it to a string for easy human reading - payload format is highly
     * application dependent. */
    snprintf(payload, sizeof(payload), "%d,%d,%d", pwrl1,pwrl2,pwrl3);

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




char* replace_char(char* str, char find, char replace){
    char *current_pos = strchr(str,find);
    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(current_pos,find);
    }
    return str;
}


// get timestamp from vzlogger EPOCH Secs * 1000 (ms) and ctr value in float
//OLD: float kwh resolution 0.1 wh
//vzlogger/data/chn0/agg 0 { "timestamp": 1636901760000, "value": 38118.691500000001 }
//vzlogger/data/chn1/agg 0 { "timestamp": 1636901760000, "value": 42601.953500000003 }

// NEW: values are in wh, *10 for 0,1 wh
//vzlogger/data/chn0/agg { "timestamp": 1683731700000, "value": 3000 }
//vzlogger/data/chn1/agg { "timestamp": 1683731700000, "value": 16000 }


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
 //      *cntr = (atof(pt) * 10000);  //convert to integer, 4 decimal places
         *cntr = (atof(pt) * 10);     //convert to integer, 0,1 wh
    }
    else {
        *cntr = -1;
    }
}


void get_wr_values (char *sbfbuf, int *pvpower, int *pvetotal, double *uac, double *pvtemp, int *pvetoday, char *debugtime)
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
            *pvetotal = 10000 * atof(pac+strlen("\"ETotal\":")); // convert to int 0.1 wh
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
        pac = strstr(sbfbuf, "\"EToday\":");
        if (pac) {
            *pvetoday = 10000 * atof(pac+strlen("\"EToday\":"));   // convert to int 0.1 wh
        }
        else {
            printf("%s Warning, no pvetoday from SBFSpot\n",debugtime);
            *pvetoday = 0;
        }
    }
    else {
        printf("%s Warning, no output from SBFSpot\n",debugtime);
        *pvpower = 0;
        *pvetotal= 0;
        *pvetoday=0;
        *pvtemp = 0.0;
        *uac=0;
    }
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
//	int epower;
//	int vpower;
	// kwh * 10000
	int vz1, vz2, ez1, ez2;

	time_t t;
	struct tm *time_info;
	char str[80];
	char outline[256];
	char instr[256];

	FILE *fp;
	int64_t mtime = 300/5;  // default sampling time in seconds, 1 minutes

	char filename[80];
    int pvpower;
    int pvetotal;  //kwh * 10000
    int pvetoday;


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

    char logtime[40];      // für Uhrzeit Vergleich für midnight log file HH:MM
    char logdate[40];     //  dd.mm.yyyy fuer midnight log für excel liste
    int midnight_flag = 0; // 0 erlaube logfile schreiben

    int pv2_etotal_sav = 0;  // PV2 Save Counters falls neue Werte kleiner als alte Werte sind
    int pv2_etoday_sav = 0;   // OpenDTU Bug bei Sonnenuntergang
    int pvetotal_sum = 0;     // etotal von PV1 und PV2
    int pvpower_sum = 0;      // sum of pvpower and pv2_power;
    int pvetotal_sav = 0;

    int conscounterwarn = 0;  // reduce number of negative conscounter warnings
    int conspowerwarn = 0;    // reduce number of negative conspower warnings
    int pvpowerwarn = 0;      // reduce number of pvpower correction warnings

    float ioff=286.0;                    // offsets ab 09.05.2023 MK7.1 mod
    float eoff=77.9;
    float pv1off=67464.609;
    float pv2off=347.89;

	// --- copy args
	if (argc < 2) {
        printf ("Usage: ehz_logger_v3 --%s;%s-- logfile [verbose|trace|debug]\n", __DATE__, __TIME__);
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
        if (!strncasecmp(argv[2], "d", 1))  {
            verbose = 1;
            trace = 1;
            dbgflag = 1;
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
                        // reload pvetotal_sav and pv2_etotal_sav from the last the midnight log values
                        // wenn die wechselrichter nicht erreichbar sind werden diese werte genommen

                        if ((fp = fopen("/var/log/ehz_logger_v2/midnight.log", "r")) == NULL) {
                            printf("%s Warning cant open Midnight Logfile\n", debugtime);
                        }
                        else {
                            while (!feof(fp)) {
                                fgets (outline, sizeof(outline), fp);
                                if (strlen(outline) > 20)
                                    strncpy(instr,outline,255);
                                else
                                    break;
                            }
                            fclose(fp);
                            if (strlen(instr) > 40) {
                                float dummy;
                                float fpv1tot;
                                float fpv2tot;
                                replace_char(instr, ';', ' ');

                                sscanf(instr,"%s %f %f %f %f %f %f %f %f %f %f %f %f %f",
                                    outline, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,&fpv1tot,&dummy,&fpv2tot, &dummy, &dummy);


                                pvetotal_sav    = (fpv1tot + pv1off)*10000.0;
                                pv2_etotal_sav =  (fpv2tot + pv2off)*10000.0;

                                printf ("Restore pvcounters from  midnight file %f %f %d %d\n\n", fpv1tot, fpv2tot,pvetotal_sav/10,pv2_etotal_sav/10);

                            }
                        }

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
                    strftime(logtime, sizeof(logtime), "%H:%M", time_info);        // only HH:MM
                    strftime(logdate, sizeof(logdate), "%d.%m.%Y", time_info); // only dd.mm.yyyy


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
                        get_wr_values (wr_buf, &pvpower, &pvetotal, &uac, &pvtemp, &pvetoday, debugtime);
                    }
                    else {
                        printf ("Warning, no wr data received, skip values %s\n", debugtime);
                        pvpower = 0;
                        pvetotal = pvetotal_sav;
                        uac = 0.0;
                        pvtemp = 0.0;
                    }
                    sbfstarted=0;  // activate sbfspot call again

                    // sicherstellen dass erzeugungzaehler immer grösser sind, nie kleiner
                    if (pvetotal < pvetotal_sav)
                        pvetotal = pvetotal_sav;
                    else
                        pvetotal_sav = pvetotal;

// specialhandling für HMS PV2:
// die Werte etotal und etoday können in Bereich Sonnenuntergang falsch sein
// Bug in OpenDTU Lösung noch offen, workaround:
                    if (pv2_flag > 0 && (pv2_etotal_sav < pv2_etotal)) {
                        pv2_etotal_sav = pv2_etotal; // save only if new value is greater
                    }
                    if (pv2_flag > 0 && (pv2_etoday_sav < pv2_etoday)) {
                        pv2_etoday_sav = pv2_etoday; // save only if new value is greater

                    }   // reset today flags at midnight
                    if (pv2_etotal_sav == 0) {
                       printf("%s Warning, no pv2_etotal from HMS_WR",debugtime);
                    }

                    pvetotal_sum = pvetotal + pv2_etotal_sav; // PV1 + PV2
                    if (pvetotal > 0 &&  pv2_etotal_sav > 0) {   // store pvetotal as backup in case of pvetotal error
                        apve[0] = apve[1];
                        apve[1] = pvetotal_sum;
                        if (pvecount < 2) {
                            pvecount++;
                            if (verbose) printf("collect pvetotal pv2_etotal last values\n");
                         }
                    }
                    else { // if pvetotal= 0 || pv2_etotal_sav == 0
                        if (pvecount >= 2) {
                            int pvewatts = ((apve[1]-apve[0])*360)/difftime;
                            pvpower_avg = pvewatts;
                            if (pvewatts >= 0 && pvewatts <= 30000) {   // in range
                                pvetotal_sum= apve[1];  // use last value
                                printf("%s Warning, no pvetotal from SBFSpot or pv2_etotal, use last value\n",debugtime);
                            }
                            else {
                                pvpower_avg = 0;
                                pvecount = 0;  // invalid backup, set counter to zero
                                apve[0] = 0;
                                apve[1] = 0;
                                printf("%s Warning, no pvetotal from SBFSpot or pv2_etotal, pvetotal_sum set zero\n",debugtime);
                             }
                        }
                    }

                    // ------ calculate all powers -----------
                    // ------ NOW: use the values of EM24 counter for vz1 and ez1 and gridpower
                    // ------ no complicate filtering anymore (hopefully)

                    if ( mb_pwr_flag == 0 || mb_imp_flag == 0 || mb_exp_flag == 0) {
                        printf("%s Error, no values from EM24\n",debugtime);
                        // exit (1);
                    }

                    vz1 = mb_imp;
                    ez1 = mb_exp;
                    gridpower = mb_pwr;



// write to midnight logfile  float kwh
// csv log:
// Datum; Z1B(EM24_IMP-IOFF); Z1L(EM24_EXP-EOFF); Z2B; Z2L; Z3E=Z1L-Z2L+Z2B-Z1B; Z3L= Z1L-Z2L; Z3B=Z2B-Z1B; PV1Today; PV1Tot-PV1Off; PV2Today; PV2Tot-PV2Off; EM24_E; EM24_I;

//                   printf("%s\n", logtime);

                   if ( (   strncmp(logtime,"23:46",5) == 0    // loesche flag um 23:46 oder 23:47
                          || strncmp(logtime,"23:47",5) == 0 )
                        && midnight_flag ==  1  )  {
                         if (verbose) puts("Reset Midnight Flag\n");

                         midnight_flag = 0;                   // logfile schreiben wieder freigegeben
                         pv2_etoday_sav = 0;                  // reset today counters at midnight
                         pv2_etoday = 0;
                    }

                    if ( (   strncmp(logtime,"23:44",5) == 0    // schreibe log um 23:44 oder 23:45
                          || strncmp(logtime,"23:45",5) == 0 )
                          && midnight_flag == 0  )  {
                         midnight_flag = 1;                   // aber nur einmal
                        //  logtime

                        float z1b,z1l, z2b, z2l, z3e, z3l, z3b, pv1today, pv1tot, pv2today, pv2tot;
                        if (verbose) puts("Write to Midnight Logfile\n");

                        z1b = ((float)mb_imp)/10000.0-ioff;
                        z1l = ((float)mb_exp)/10000.0-eoff;
                        z2b = ((float)vz2)/10000.0;
                        z2l = ((float)ez2)/10000.0;
                        z3e = z1l-z2l + z2b-z1b;
                        z3l = z1l-z2l;
                        z3b = z2b-z1b;
                        pv1today = ((float)pvetoday)/10000.0;
                        pv1tot = ((float)pvetotal)/10000.0 - pv1off;
                        pv2today = ((float)pv2_etoday_sav)/10000.0;
                        pv2tot = ((float)pv2_etotal_sav)/10000.0 - pv2off;

                        sprintf(outline,"%s;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f;%8.1f\n",
                            logdate, z1b, z1l, z2b, z2l, z3e, z3l, z3b, pv1today,pv1tot,pv2today,pv2tot, z1b+ioff, z1l+eoff);


                        if ((fp = fopen("/var/log/ehz_logger_v2/midnight.log", "a+")) == NULL) {
                            printf("%s Error cant create or open Midnight Logfile\n", debugtime);
                            exit(1);
                        }
                            fputs(outline, fp);
                            fclose(fp);
                    }

                    if (verbose) printf("kwh EM24_B %06.4f EM24_L %06.4f Z2_B %06.4f Z2_L %06.4f PV %06.4f PV2 %06.4f\n",
                        ((double)vz1) / 10000, ((double)ez1) / 10000, ((double)vz2) / 10000, ((double)ez2) / 10000, ((double)pvetotal)/10000, ((double)pv2_etotal_sav)/10000);


                    pvpower_sum = pvpower+pv2_power; // add PV1 and PV2

                    if (pvecount >= 2)
                        pvpower_avg  = ((apve[1]-apve[0])*360)/difftime;
                    else
                        pvpower_avg  =  pvpower_sum; // first runs


//                    vpower = (double)((vz2 - vz1) * 360) / difftime;
//                    epower = (double)((ez2 - ez1) * 360) / difftime;
//                    gridpower = vpower - epower;



                    //Gesamtverbrauch (unit Watt):
                    // PVErzeugung - Einspeisung + Netzbezug

                    conspower_raw = pvpower_avg + gridpower;

                    if (pvecount >= 2)
                        conspower_filt = conspower_filt + conspower_raw - conspower_filt/10;
                    else
                        conspower_filt = 10 * conspower_raw;  // first runs


//                  correction of pvpower to avoid negative consumption
                    pvpower_corr = 0;
                    if (pvpower_sum > 0) {
                        if (pvpower_avg  + gridpower <  MINCONS) {  // Verbrauch < MINCONS (MINCONS=100 W)
                            pvpower_corr = -gridpower + max(MINCONS, conspower_filt/10);   // Korrektur verschönern, mind. Verbrauch 100W oder letzter Mittelwert

                            if (pvpowerwarn > 20) {   //warning every twenty corrections
                                printf ("Warning, corrected pvpower: %s old: %d new: %d ", debugtime,pvpower_avg,pvpower_corr);
                                printf("pvpower_avg %d gridpower %d conspower_filt/10  %d\n", pvpower_avg,gridpower,conspower_filt/10 );
                                pvpowerwarn = 0;
                            }
                            pvpowerwarn++;
                        }
                        else
                            pvpower_corr = pvpower_avg;
                    }
 //XXX deactivate pvpower_corr and corrected conspower
                     pvpower_corr = pvpower_sum;   // use uncorrected values

                     conspower = pvpower_corr + gridpower;
                     if (conspower < 0) conspower = 0;   // XXX simply set to zero



                   // publish qrid watts for evcc (evcc gets values direct from mbmd)
                   // publish_power(mosq, EHZTOPIC_GRIDPWR, gridpower);

                   // publish pvpowercorr watts for evcc (and victron)
                    publish_power(mosq, EHZTOPIC_PVPWR,  pvpower_corr);

                   // publish energy pvtoday in 0.1Wh for victron, divide by 10000 for kwh
                    publish_power(mosq, EHZTOPIC_PVTODAY,  pvetoday+pv2_etoday_sav);

                   // publish energy pvtotal in 0.1Wh for victron, divide by 10000 for kwh
                    publish_power(mosq, EHZTOPIC_PVTOTAL,  pvetotal_sum);

                    // publish pvpowers per phase, added balkon on l2
                    publish_powerl123(mosq,EHZTOPIC_PVPWRL123, pvpower+pv2_l1_pwr,pv2_l2_pwr+pv2_l2_b_pwr,pv2_l3_pwr);


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
                             "time,pvetotal,pvpower,pv2_etotal_sav,pv2_power,vz2,ez2,gridpower,conspower,conspower_raw,conspower_filt,pvpower_avg,pvpower_corr");
                        }
                        else {
                            sprintf(outline,"%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                              str,pvetotal,pvpower,pv2_etotal_sav,pv2_power,vz2,ez2,gridpower,conspower,conspower_raw,conspower_filt/10,pvpower_avg,pvpower_corr);
                        }

                        if ((fpm = fopen("/var/log/ehz_logger_v2/minute.log", "a+")) == NULL) {
                            printf("%s Error cant create or open logfile %s\n", debugtime, argv[2]);
                            exit(1);
                        }
                        fputs(outline, fpm);
                        fclose(fpm);
                    }


                    if (conspower < 0) {   // Battery Operation: neg. conspower still possible
                        conspowerwarn++;
                        if (conspowerwarn > 50) {
                            conspowerwarn=0;
                            printf("%s Warning: neg. consumption, correction\n",debugtime);
                        }
                        conspower = (apvpower[2]-aepower[2]-avpower[2]+ apvpower[0]-aepower[0]-avpower[0])/2;
                    }
                    // Werte für Mittelwertkorrektur speichern (alles integer)
                    apvpower[0]=apvpower[1]; apvpower[1]=apvpower[2]; apvpower[2]=pvpower;
;

                    // Gesamtzaehlerstand Verbrauch (unit Wh):
                    // PVZaehler-Einspeisezaehler+Bezugszaehler
                    // wenn Gesamtzaehlerstand < Gesamtzaehlerstand_Previous,
                    // dann nutze PreviousZahlerstnd
                    // beim ersten durchlauf könnte ein initialer offset gesetzt werden
                    // damit der initiale Wert vz2 (virtueller Differenz Zaehler) ist



                    if (firstloop) {
                        conscounterprev = pvetotal_sum - ez1 + vz1;
                        firstloop = 0;
                    }
                    conscounter = pvetotal_sum - ez1 + vz1;


                    if (conscounter < conscounterprev)
                    {
                       conscounterwarn++;
                       if (conscounterwarn > 50){
                            printf("%s Warning: neg. conscounter, correction\n",debugtime);
                            conscounterwarn = 0;
                       }
                       conscounter=conscounterprev;
                    }
                    conscounterprev=conscounter;

                    if (verbose)
                        printf("gridpower pvpower_sum pvetotal pvpower pv2_etotal pv2_power conspower conscounter\n");

                    if (verbose)
                        printf("%8d %8d %8d %8d %8d %8d %8d %8d\n",
                            gridpower,pvpower_sum,pvetotal,pvpower,pv2_etotal_sav,pv2_power,conspower,conscounter/10);

                    // ---- copy values for next loop
                    time1=time2;
//                    ez1=ez2;
//                    vz1=vz2;
                    // read timedate
                    time(&t);
                    time_info = localtime( &t );
                    strftime(str, sizeof(str), "%Y%m%d,%H:%M", time_info);

                      // set timedate, for debug
                    strftime(debugtime, sizeof(str), "%Y%m%d,%H:%M:%S", time_info);

                    // format for logfile, optimize fields for curl for delayed or repeated upload if necessary
                    if (verbose)
                        printf ("date,pvetotal_sum/10,pvpower_sum,conscounter/10,conspower,vz1,ez1,gridpower,pvpower,pvetoday/10,pv2_power,pv2_etoday_sav/10,vz2,ez2\n");


                    sprintf(outline,"%s,%d,%d,%d,%d;%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                       str,(int)pvetotal_sum/10,(int)pvpower_sum,conscounter/10, conspower,vz1,ez1,gridpower,pvpower,pvetoday/10, pv2_power,pv2_etoday_sav/10,vz2,ez2);

                    if (verbose)
                        puts(outline);


                    // rough sanity check of values:
                    int invalid = 0;
                    if (    pvpower_sum > 20000  || pvpower_sum < 0
                        || conspower > 50000 || conspower < 0
                        || gridpower > 50000 || gridpower < -50000) {
                            printf ("%s Warning skip out of range values, no storage \n",debugtime);
                            printf ("date,pvetotal_sum/10,pvpower_sum,conscounter/10,conspower,vz1,ez1,gridpower,pvpower,pvetoday/10,pv2_power,pv2_etoday_sav/10,vz2,ez2\n");

                            printf ("date,pvetotal_sum/10,pvpower_sum,conscounter/10,conspower,vz1,ez1,gridpowe,pvpower,pvetoday/10,pv2_power,pv2_etoday_sav/10\n");
                            puts(outline);
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

//                        send_data (time_info,pvetotal_sum/10, pvpower_sum,conscounter/10 , conspower, uac, pvtemp);
// new: send 4 extended data for PV1 and PV2, donation required
                        send_data (time_info,pvetotal_sum/10, pvpower_sum,conscounter/10 , conspower, uac, pvtemp,pvpower,pvetoday/10,pv2_power,pv2_etoday_sav/10 );

                    }
                }  // chan0_flag && chan1_flag
            }  // endless loop after start
       } // else if (run)
    }  // while(run)

    mosquitto_lib_cleanup();


    return rc;
}
