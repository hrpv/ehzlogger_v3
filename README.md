# ehzlogger_v3

Use of an SMA PV inverter with Bluetooth and an EHZ bidirectional meter from 2010 together with evcc. 
In order for the PV charging to work well with evcc, the grid power and the PV output should be measured. 

# Installation
required lib libmosquitto.so.1

check if already installed 
/usr/lib/x86_64-linux-gnu/libmosquitto.so.1

or /usr/lib/arm-linux-gnueabihf/libmosquitto.so.1

if not installed
sudo apt-get install libmosquitto-dev libmosquitto1 mosquitto-dev


# make

install service pvlogger: 
# instpvlogger.sh



# additional software

vzlogger:
--------
https://wiki.volkszaehler.org/software/controller/vzlogger/installation_en
https://wiki.volkszaehler.org/howto/emh_pv-anlage

sbfspot:
---------
https://github.com/SBFspot/SBFspot/
No local database is used, data are stored in a logfile and on pvoutput.org
sbfspot is called with -nocsv -nosql Parameter
SBFspot  -v -finq -nocsv -nosql -mqtt

# NOTE: 
since version 3.8.3 a 5 minute cronjob is installed, remove this
because it collides with the 1 minute call from ehzlogger

sudo cat -v /var/spool/cron/crontabs/pi

```bash
# SBFspot
*/5 6-22 * * * /usr/local/bin/sbfspot.3/daydata 
55 05 * * * /usr/local/bin/sbfspot.3/monthdata
```

comment the two lines with `#` with: crontab -e


evcc:
------ 
https://evcc.io
sample meters in evcc.yaml
```yaml 
meters:
# gridmeter output wird erzeugt von ehzlogger_v3 (service pvlogger) alle 60 s (vpower-epower)
- name: gridmeter # Name des Zählers. Falls du diesen änderst, musst du die site-Konfiguration auch anpassen.
  type: custom   # Typ des Zählers
  power:
    source: mqtt
    broker: 127.0.0.1:1883
    topic: ehzmeter/power
    timeout: 360s # don't accept values older than timeout

#
# pvmeter ist der output von ehzlogger_v3 (abgeleitet von sbfspot), wird alle 60s von ehzlogger_v3 (service pvlogger) 
- name: pvmeter # Name des Zählers. Falls du diesen änderst, musst du die site-Konfiguration auch anpassen.
  type: custom   # Typ des Zählers
  power:
    source: mqtt
    broker: 127.0.0.1:1883
    topic: ehzmeter/pvpower
    timeout: 360s # don't accept values older than timeout
```

account on pvoutput.org
-------------------------
see pvoutkeys.h

picture
![Contribution guidelines for this project](docs/CONTRIBUTING.md)
