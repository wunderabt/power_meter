# Paket Radio for Power Meter

idea: use 433MHz or 866MHz band to remotely read the power meter

Transceiver: [RFM69](https://www.mikrocontroller.net/articles/RFM69)

attach a sender to the power meter

Power Meter read-out via IR: https://wiki.volkszaehler.org/hardware/controllers/ir-schreib-lesekopf-ttl-ausgang

Sender:
- [Moteino](https://lowpowerlab.com/guide/moteino/)
- [Adafruit Feather](https://learn.adafruit.com/adafruit-feather-32u4-radio-with-rfm69hcw-module)

Receiver:
- [Arduino with Ethernet-Shield and SD-Card Logger](https://store.arduino.cc/arduino-ethernet-shield-2)

The reciver logs all incoming packages and makes them available for queries via network over ethernet.

Shops:
- https://www.distrelec.de (adafruit)
- https://www.makershop.de (adafruit)
- https://www.berrybase.de

Protocol information:
- OBIS codes https://www.promotic.eu/en/pmdoc/Subsystems/Comm/PmDrivers/IEC62056_OBIS.htm
- SML structure example http://www.stefan-weigert.de/php_loader/sml.php

## Structure

```
Power Meter      Adafruit Feather 32u4   866MHz   Arduino Uno       Banana-Pi
ISKRA MT631  --> IR Read Head           --------> Ethernet     -->  InfluxDB
                 RFM69                            RFM69             Grafana
                                                  SD-Card
```

## Sender

The sender wakes up from deep sleep every hour, awaits an SML (**S**mart **M**essage **L**anguage) telegram from the power meter (which sends one every second through the infrared interface) and stores timestamp and power reading in RAM, returns to sleep. When no more entries can be held in RAM it powers up the RFM69 radio and sends all stored readings.

According to the datasheet the adafruit feather consumes 300uA in deep sleep so that in theory a 500mAh battery is sufficient for 500mAh/300uA = 1666.7h = 69.4 days of operation. With the high power phase every hour it will hopefully still last over a month per charge.

### SML fields

We are interested in two fields from an SML telegram (see the full dump in `raw-data/serial-dump-annotate.hex` in this repository)

(1) Timestamp (i.e. uptime)
```
[..]
        63 7 1                                   # getListResponse
        77                                       # List, 7 entries
            1                                    # clientId, no value
            B XX XX XX XX XX XX XX XX XX XX      # serverId
            7 1 0 62 A FF FF                     # OBIS manufacturer
            72                                   # List, 2 entries
                62 1                             # choice: secIndex
                65 0 11 9C 33                    # secIndex (uptime)
[..]
        63 7 1                                   # getListResponse
        77                                       #
            1                                    #
            B XX XX XX XX XX XX XX XX XX XX      # serverId
            7 1 0 62 A FF FF                     # OBIS manufacturer
            72
                62 1                        
                65 0 11 9C 34                    # uptime
```

The uptime is `0x00119C33 = 1154099 sec = 13d8h` and `0x00119C34 = 1154100 sec` respectively. As sanity check: the telegram was received on 2021/04/01 at around 19:00 and the power meter was installed on 2021/03/19 at around 11:00. That matches 13 days and 8 hours.

(2) Power Reading
```
                77                               # List, 7 entries
                    7 1 0 1 8 0 FF               # OBIS 1.8.0*255
                    65 0 10 1 4                  # status ?
                    1                            # valTime
                    62 1E                        # unit: 1e = "Wh"
                    52 3                         # scaler, 3 = k
                    62 2E                        # value: 0x2e = 46 kWh
                    1                            # valueSignature
```

Field lengths can vary. The current `62 2E = 46kWh` will become longer and the 2nd nibble indicates the field length, for example a future reading could be `64 1 2 3 = 291kwH`.

To simplify the code on the sender it will not implement a full SML parser that takes varying field length into account. It will be rather dumb: it checks for the occurence of the interesting OBIS code in the byte stream and then store sufficiently many bytes after that to capture the values. The extraction of the values from that SML snippet is done in postprocessing on the receiver.

### SML snippet storage

#### Timestamp

The timestamp is identified by the byte sequence

    7 1 0 62 A FF FF

and the following 12 bytes are stored although 8 byte are sufficient at the moment but we want to allow a growing time stamp field.

Example
```
        63 7 1
        77
            1
            B XX XX XX XX XX XX XX XX XX XX
            7 1 0 62 A FF FF               <== Marker
            72                             <== start to store
                62 1                        
                65 0 11 9C 34              <== 8th byte
            74
                77
                    7 1 0 60 32 1 1              # OBIS
                    1
                    1
                    1
                    1
                    4 49 53 4B                   # 49 53 48 = "ISK"
                    1
```

#### Power

The power reading is identified by the byte sequence

    7 1 0 1 8 0 FF

and the following 20 bytes are stored although 12 bytes are sufficient at the moment but we want to allow a growing power field.

Example:
```
                77
                    7 1 0 1 8 0 FF         <== Marker
                    65 0 10 1 4            <== start to store 
                    1
                    62 1E
                    52 3
                    62 2E                  <== 12th byte
                    1
                77
                    7 1 0 2 8 0 FF               # OBIS 2.8.0*255
                    1
                    1
                    62 1E                        # "Wh"
                    52 3                         # "k"
                    62 0
                    1
```

### RAM

The following information is stored each hour:

- timestamp: 12 bytes
- power reading: 20 bytes
- battery level of the sender: 2 bytes (int)

A total of 34 bytes - after 24h this amounts to 816 bytes which fits into the 2KB memory.

## Receiver

The receiver is an Arduino with RFM69, Ethernet and SD-Card. It loops over events from Ethernet and the radio. When it receives new data via radio from the sender, which should be 720 bytes every 24h, it converts them to a CSV format and appends it to data on the SD-Card. The content of the SD-Card can be accessed through the http protocol over the ethernet shield.