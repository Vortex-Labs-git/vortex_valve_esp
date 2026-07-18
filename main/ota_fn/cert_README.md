# Configs

nano partitions.csv

# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x4000
otadata,    data, ota,      0xd000,   0x2000
phy_init,   data, phy,      0xf000,   0x1000
ota_0,      app,  ota_0,    0x10000,  0x180000
ota_1,      app,  ota_1,    ,         0x180000


Step 2 — menuconfig
bashidf.py menuconfig
Navigate with arrow keys, Enter to open, Space to toggle, S to save, Q to quit.
a) Partition table:
Partition Table  --->
    Partition Table (Single factory app, no OTA)  --->
        (X) Custom partition table CSV
    (partitions.csv) Custom partition CSV file        ← leave as default name
b) Flash size:
Serial flasher config  --->
    Flash size (2 MB)  --->
        (X) 4 MB
This one matters — if it's set to 2 MB, the build will complain that the partition table exceeds flash.
c) Rollback:
Bootloader config  --->
    [*] Enable app rollback support
(It's near the bottom of Bootloader config, listed as "Enable app rollback support".)
Save with S, confirm, quit with Q.



1. Download the Google root CA

curl -o ota_ca_cert.pem https://i.pki.goog/r4.pem

2. Verify the file is correct

openssl x509 -in ota_ca_cert.pem -noout -subject -enddate

subject=C=US, O=Google Trust Services LLC, CN=GTS Root R4
notAfter=Jun 22 00:00:42 2036 GMT

3. And confirm it actually validates your server

openssl s_client -connect www.vortexlabsofficial.com:443 \
  -servername www.vortexlabsofficial.com \
  -CAfile ota_ca_cert.pem </dev/null 2>/dev/null | grep "Verify return code"