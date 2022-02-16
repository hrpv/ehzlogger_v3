#!/bin/sh
/usr/bin/ehzlogger_v3 /var/log/ehz_logger_v2/emh_logfile  >/var/log/ehz_logger_v2/emh_errlog 2>&1

#/usr/bin/ehz_logger_v2 /dev/serial0 /var/log/ehz_logger_v2/emh_logfile 300  >/var/log/ehz_logger_v2/emh_errlog 2>&1

#uncomment to force restart of service (if activated)
#exit 1



