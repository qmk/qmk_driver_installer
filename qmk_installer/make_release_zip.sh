#!/bin/sh
rm -f drivers.txt qmk_driver_installer.zip
wget https://github.com/qmk/qmk_firmware/raw/master/util/drivers.txt
zip qmk_driver_installer.zip qmk_driver_installer.exe drivers.txt install_all_drivers.bat install_connected_drivers.bat
