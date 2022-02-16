#!/bin/sh
# install ehzlogger_v3 as a service
sudo systemctl stop pvlogger
sudo systemctl disable pvlogger
sudo cp bin/Debug/ehzlogger_v3 /usr/bin
sudo cp install/emhlogstart.sh /usr/bin
sudo chmod a+x /usr/bin/emhlogstart.sh
test -d /var/log/ehzlogger_v2 || sudo mkdir -p /var/log/ehzlogger_v2
sudo cp install/pvlogger.service /etc/systemd/system
sudo systemctl enable pvlogger
sudo systemctl start pvlogger
