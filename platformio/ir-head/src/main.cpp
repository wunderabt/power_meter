#include <SPI.h>
#include <RH_RF69.h>
#include <RHReliableDatagram.h>
#include <Adafruit_SleepyDog.h>

// CONSTANTS
#define RFM69_DEST_ADDRESS   1
// change addresses for each client board, any number :)
#define RFM69_ADDRESS        2
#define RFM69_FREQ         999.9

  #define RFM69_CS      8
  #define RFM69_INT     7
  #define RFM69_RST     4
  #define LED           13

#define VBATPIN A9
#define IRPOWERPIN A5
const byte SML_START_SEQUENCE[] = {0x1B, 0x1B, 0x1B, 0x1B, 0x01, 0x01, 0x01, 0x01};
const byte SML_OBIS_TIMESTAMP[] = {0x07, 0x01, 0x00, 0x62, 0x0A, 0xFF, 0xFF};
const byte SML_OBIS_POWER[]     = {0x07, 0x01, 0x00, 0x01, 0x08, 0x00, 0xFF};

const int num_timestamp_bytes = 12;
const int num_power_bytes = 20;
const int num_memory_entries = 24; // entries for one day, one entry each hour
const uint16_t sample_interval = 3600; // 3600s = 1h

const uint8_t rf69_key[] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                             0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
const uint8_t rf69_retries = 20;
const int     rf69_retry_pause = 2000; // 2s

// GLOBALS
// Singleton instance of the radio driver
RH_RF69 rf69(RFM69_CS, RFM69_INT);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, RFM69_ADDRESS);

uint8_t rf69_buf[RH_RF69_MAX_MESSAGE_LEN];

struct PowerReading {
  byte timestamp[num_timestamp_bytes];
  byte power[num_power_bytes];
  int bat_volt;
} readings[num_memory_entries];
int cur_reading_idx = 0;



void setup() {
  // USB debug port
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  // IR Header from power meter
  pinMode(IRPOWERPIN, OUTPUT);
  digitalWrite(IRPOWERPIN, LOW);
  Serial1.begin(9600);
  Serial1.setTimeout(1500); // a bit more than 1 sec because the powermeter only sends every 1 sec

  // RFM69 init
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);

  // manual reset
  digitalWrite(RFM69_RST, HIGH);
  delay(10);
  digitalWrite(RFM69_RST, LOW);
  delay(10);

  if (!rf69_manager.init()) {
    Serial.println("RFM69 radio init failed");
    while (1);
  }
  Serial.println("RFM69 radio init OK!");
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for low power module)
  // No encryption
  if (!rf69.setFrequency(RFM69_FREQ)) {
    Serial.println("setFrequency failed");
  }

  // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
  // ishighpowermodule flag set like this:
  rf69.setTxPower(20, true);  // range from 14-20 for power, 2nd arg must be true for 69HCW

  rf69.setEncryptionKey(rf69_key);

  Serial.print("RFM69 radio @");  Serial.print((int)RFM69_FREQ);  Serial.println(" MHz");

  // send a message that we restarted
  char start_msg[] = "restart";
  if (rf69_manager.sendtoWait(start_msg, sizeof(start_msg), RFM69_DEST_ADDRESS)) {
    // Now wait for a reply from the server
    uint8_t len = sizeof(rf69_buf);
    uint8_t from;
    if (rf69_manager.recvfromAckTimeout(rf69_buf, &len, 2000, &from)) {
      Serial.print("Got reply from #"); Serial.print(from);
      Serial.print(" [RSSI :");
      Serial.print(rf69.lastRssi());
      Serial.print("] : ");
    } else {
      Serial.print("got no reply");
    }
  } else {
    Serial.print("send restart message failed");
  }

  // RFM96 starts powered off
  rf69.sleep();

  // pause for 20s before we go into deep sleep where USB turns off
  // (this allows us to press reset and have time to initiate upload of new firmware)
  delay(20000);
}

void long_sleep(uint16_t secs) {
  // according to https://forums.adafruit.com/viewtopic.php?f=8&p=721794#p721573
  // the max sleep time for 32u4 via watchdog is 8 sec
  int loop_count = secs / 8;
  for (int i=0; i<loop_count; i++) {
    Watchdog.sleep(8000);
  }
  Watchdog.sleep((secs % 8)*1000);
}


bool find_sequence(const byte *seq, size_t len) {
  // it appears as if Stream.find(..) doesn't work with 0x00 bytes (?)
  // so I wrote this naive matcher
  size_t pos = 0;
  int count = 0;
  while (1) {
    if (Serial1.available()) {
      byte inByte = Serial1.read();
      if (pos > 0 && inByte != seq[pos]) {
        pos = 0;
      } else if (inByte == seq[pos]) {
        pos++;
      }
      if (pos == len) {
        return true;
      }
      count++;
      if (count > 999) {
        return false; // abort search
      }
    }
  }
}

// returns true when send was successful
bool send_data(byte *data, int data_len) {
  if (rf69_manager.sendtoWait(data, data_len, RFM69_DEST_ADDRESS)) {
    // Now wait for a reply from the server
    uint8_t len = sizeof(rf69_buf);
    uint8_t from;
    if (!rf69_manager.recvfromAckTimeout(rf69_buf, &len, 2000, &from)) {
      return false;
    }
  } else {
    return false;
  }
  return true;
}


void loop() {
  // read timestamp and power from IR and store in RAM
  digitalWrite(IRPOWERPIN, HIGH); // power up the IR head
  delay(100);
  if (find_sequence(SML_START_SEQUENCE, sizeof(SML_START_SEQUENCE))) {
    if (find_sequence(SML_OBIS_TIMESTAMP, sizeof(SML_OBIS_TIMESTAMP))) {
      Serial1.readBytes(readings[cur_reading_idx].timestamp, num_timestamp_bytes);
      if (find_sequence(SML_OBIS_POWER, sizeof(SML_OBIS_POWER))) {
        Serial1.readBytes(readings[cur_reading_idx].power, num_power_bytes);
        readings[cur_reading_idx].bat_volt = analogRead(VBATPIN);
        cur_reading_idx++;
      } else { Serial.println("no SML power sequence found"); }
    } else { Serial.println("no SML timestamp sequence found"); }
  } else { Serial.println("no SML start sequence found"); }
  digitalWrite(IRPOWERPIN, LOW); // power down the IR head

  // debug print
  /* Serial.print("TIMESTAMP: ");
  for (int i = 0; i < num_timestamp_bytes; i++) {
    Serial.print(readings[cur_reading_idx-1].timestamp[i], HEX); Serial.print(" ");
  }
  Serial.println();
  Serial.print("POWER: ");
  for (int i = 0; i < num_power_bytes; i++) {
    Serial.print(readings[cur_reading_idx-1].power[i], HEX); Serial.print(" ");
  }
  Serial.println(); */


  if (cur_reading_idx == num_memory_entries) {
    // RAM full
    // Send a message to the DESTINATION!
    for (int i=0; i<num_memory_entries; i++) {
      uint8_t retries = 0;
      while (retries < rf69_retries) {
        retries++;
        bool send_success = send_data(readings[i].timestamp, num_timestamp_bytes) && \
                            send_data(readings[i].power, num_power_bytes) && \
                            send_data((uint8_t*)&readings[i].bat_volt, 2);
        if (send_success) {
          break;
        }
        delay(rf69_retry_pause);
      }
    }
    cur_reading_idx = 0;
    rf69.sleep();
  }
  long_sleep(sample_interval);
}
