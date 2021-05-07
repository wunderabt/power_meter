/*
  Power Meter Gateway

  receives data packets through an RFM69 and stores them on SD-Card
  stored data is served through http via ethernet shield

  by Uwe Brandt

  SPI chip selects:
  10 - ethernet
   4 - SD Card
   5 - RFM69
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SD.h>
#include <RH_RF69.h>
#include <RHReliableDatagram.h>

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};
IPAddress ip(192, 168, 178, 177);
const unsigned int localPort = 8888;      // local port to listen on
EthernetUDP Udp;

const uint8_t sdcard_cs_pin   =  4;
const uint8_t rfm69_cs_pin    =  5;
const uint8_t rfm69_int_pin   =  3;
const uint8_t rfm69_rst_pin   =  2;
const uint8_t ethernet_cs_pin = 10;

#define RF69_FREQ         999.9
#define RF69_ADDRESS        1
#define SD_FILENAME "readings.hex"

uint8_t rf69_key[] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                       0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

// Singleton instance of the radio driver
RH_RF69 rf69(rfm69_cs_pin, rfm69_int_pin);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, RF69_ADDRESS);
uint8_t msg_buf[RH_RF69_MAX_MESSAGE_LEN];

void spi_select(uint8_t pin) {
  if (pin == sdcard_cs_pin) {
    digitalWrite(ethernet_cs_pin, HIGH);
    digitalWrite(rfm69_cs_pin,    HIGH);
  } else if (pin == ethernet_cs_pin) {
    digitalWrite(sdcard_cs_pin,   HIGH);
    digitalWrite(rfm69_cs_pin,    HIGH);
  } else {
    digitalWrite(ethernet_cs_pin, HIGH);
    digitalWrite(sdcard_cs_pin,   HIGH);
  }
  digitalWrite(pin, LOW); // LOW = active
}

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // SD-Card Setup
  pinMode(sdcard_cs_pin, OUTPUT);

  // RFM69 Setup
  pinMode(rfm69_cs_pin, OUTPUT);
  spi_select(rfm69_cs_pin);
  pinMode(rfm69_rst_pin, OUTPUT);
  digitalWrite(rfm69_rst_pin, LOW);
  digitalWrite(rfm69_rst_pin, HIGH); // manual reset
  delay(10);
  digitalWrite(rfm69_rst_pin, LOW);
  delay(10);

  if (!rf69_manager.init()) {
    // Serial.println("RFM69 radio init failed");
    while (1);
  }
  rf69.setFrequency(RF69_FREQ);
  //Serial.println("RFM69 radio init OK!");
  /* if (!rf69.setFrequency(RF69_FREQ)) {
    Serial.println("setFrequency failed");
  } */
  // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
  // ishighpowermodule flag set like this:
  rf69.setTxPower(20, true);  // range from 14-20 for power, 2nd arg must be true for 69HCW

  // The encryption key has to be the same as the one in the server
  rf69.setEncryptionKey(rf69_key);
  //Serial.print("RFM69 radio @");  Serial.print((int)RF69_FREQ);  Serial.println(" MHz");

  // Ethernet Setup
  pinMode(ethernet_cs_pin, OUTPUT);
  spi_select(ethernet_cs_pin);
  Ethernet.begin(mac, ip);

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    //Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
  Udp.begin(localPort);
  Serial.println("ready");
}

void send_udp_message(char* msg) {
  spi_select(ethernet_cs_pin);
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  Udp.write((char *)msg);
  Udp.endPacket();
} 

void loop() {
  // listen for incoming clients
  spi_select(ethernet_cs_pin);
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    spi_select(sdcard_cs_pin);
    if (SD.begin(sdcard_cs_pin)) {
      File dataFile = SD.open(SD_FILENAME, FILE_READ);
      msg_buf[RH_RF69_MAX_MESSAGE_LEN-1] = '\0';
      if (dataFile) {
        while (dataFile.available()) {
          dataFile.read(&msg_buf, RH_RF69_MAX_MESSAGE_LEN-1);
          send_udp_message((char *)msg_buf);
          spi_select(sdcard_cs_pin);
        }
        dataFile.close();
      }
    }
    // send a UDP package with a "END" sequence
    char eot[] = "\n\nEOT";
    send_udp_message(eot);
  }

  spi_select(rfm69_cs_pin);
  if (rf69_manager.available())
  {
    // Wait for a message addressed to us from the client
    uint8_t len = sizeof(msg_buf);
    uint8_t from;
    if (rf69_manager.recvfromAck(msg_buf, &len, &from)) {
      Serial.println(len);
      spi_select(sdcard_cs_pin);
      if (SD.begin(sdcard_cs_pin)) {
          File dataFile = SD.open(SD_FILENAME, FILE_WRITE);
          if (dataFile) {
            for (uint8_t i = 0; i < len; i++) {
              dataFile.print(msg_buf[i], HEX); dataFile.print(" ");
            }
            dataFile.println();
            dataFile.close();
          } // else {
            // Serial.println("Can't write file");
          // }
      } // else {
        //Serial.println("poop");
      // }
      spi_select(rfm69_cs_pin);
      uint8_t ack[] = { 0xAA };
      rf69_manager.sendtoWait(ack, 1, from);
    }
  }
}
