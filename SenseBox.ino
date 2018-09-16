#include <SD.h>
#include <SPI.h>
#include <DHT.h>
#include <DS1302RTC.h>
#include <Time.h>
#include "endianness.h"

//time constants in seconds
const long MEASUREMENT_INTERVAL   = 60*60;
const long SLEEP_INTERVAL         = 5*60;

//pin numbers
#define PIN_DHT           7
#define PIN_RTC_CLK       21
#define PIN_RTC_DAT       20
#define PIN_RTC_RST       19
#define PIN_SD_CS         10

//ble request codees
#define REQUEST_CODE_ACTUAL    0x1D
#define REQUEST_CODE_LIST      0x1E
#define REQUEST_CODE_HISTORY   0x1F

//ble response flags
#define RESPONSE_FLAG_TIMESTAMP     0x00
#define RESPONSE_FLAG_TEMPERATURE   0x01
#define RESPONSE_FLAG_HUMIDITY      0x02
#define RESPONSE_FLAG_END           "END"
#define RESPONSE_FLAG_UDEF          "UDEF"
#define RESPONSE_FLAG_AT            0x2B   //each at command response start with '+' character

//other constants
#define TYPE_DHT DHT22
#define AT_SLEEP              "AT+SLEEP\r\n"
#define FILE_EXTENSION        ".DAT"
#define MEASUREMENT_REPEATS   5

//structures
struct BleData {
  int size;
  byte* data;
};

//global variables
DHT dht(PIN_DHT, TYPE_DHT);
DS1302RTC RTC(PIN_RTC_RST, PIN_RTC_DAT, PIN_RTC_CLK);
long lastMeasurement;
volatile long lastCommunication;
volatile bool isSleeping;
volatile struct BleData bleData;

//declaration of functions
long getTimestamp();
float getTemperature();
float getHumidity();
char* getFileName();
char* createFileData(long);
void sendActualData();
void sendListOfFiles();
void sendHistory(char*);
BleData readDataFromBle();
void writeDataToBle(byte*, int);
void enterBleSleepMode();

/*
 * Initial setup.
 */
void setup() {
  Serial.begin(9600);   //usb serial
  Serial1.begin(9600);  //rx-tx serial

  //TODO remove following loop in production
  //wait for usb serial to connect
  while(!Serial) {  }

  //setup global variables
  isSleeping = false;
  bleData = BleData{0, NULL};

  //init RTC module
  Serial.print("Initializing RTC module... ");
  setSyncProvider(RTC.get);
  if (timeStatus() == timeSet) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }

  //init SD card module
  Serial.print("Initializing SD card module... ");
  pinMode(PIN_SD_CS, OUTPUT);
  if (SD.begin(PIN_SD_CS)) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }
}

/*
 * Main loop.
 */
void loop() {
  long timestamp = getTimestamp();

  //check sleep mode time
  if (!isSleeping && lastCommunication < timestamp) {
    enterBleSleepMode();
  }
 
  //check measurement time
  if (lastMeasurement < timestamp) {
    lastMeasurement = timestamp + MEASUREMENT_INTERVAL;

    char* fileName = getFileName();
    File file = SD.open(fileName, FILE_WRITE);
    if (file) {
      char* fileData = createFileData(timestamp);
      file.println(fileData);
      file.close();
      Serial.print("File data saved: ");
      Serial.println(fileData);

      free(fileData);
    } else {
      Serial.print("Cannot open file: ");
      Serial.println(fileName);
    }

    free(fileName);
  }

  //check if new ble request received
  if(bleData.data != NULL) {
    //TODO remove logging bellow
    Serial.print("Received:");
    for(int i = 0; i < bleData.size; i++) {
      Serial.print(bleData.data[i], HEX);
    }
    Serial.println("");

    switch(bleData.data[0]) {
      case REQUEST_CODE_ACTUAL:
        sendActualData();
        break;
      case REQUEST_CODE_LIST:
        sendListOfFiles();
        break;
      case REQUEST_CODE_HISTORY:
        if (bleData.size == 5) { //request code + date(4 bytes)
          char date[8 + strlen(FILE_EXTENSION)];  //e.g. 20180908.DAT -> 08.Sept.2018
          sprintf(date, "%ld%s", *(long*)(bleData.data + 1), FILE_EXTENSION);
          sendHistory(date);
        }
        break;
      case RESPONSE_FLAG_AT:
        //ignore
        break;
      default:
        writeDataToBle(RESPONSE_FLAG_UDEF, strlen(RESPONSE_FLAG_UDEF));
        break;
    }
    
    free(bleData.data);
    bleData.data = NULL;
  }
}

/*
 * Handler of incomming data from ble serial RX.
 */
void serialEvent1() {
  bleData = readDataFromBle();
  if(bleData.size) {
    isSleeping = false;
    lastCommunication = getTimestamp() + SLEEP_INTERVAL;
  }
}

/**
 * Get current time as seconds since Jan 1 1970.
 */
long getTimestamp() {
  return (long) now();
}

/**
 * Get current temperature value from DTH sensor.
 */
float getTemperature() {
  float tempAvarage = 0;
  int index = 0;
  while (index != MEASUREMENT_REPEATS) {
    float temp = dht.readTemperature();
    if (temp != temp) {   //is NaN
      delay(10);
      continue;
    }
    tempAvarage += temp;
    index++;
    delay(10);
  }
  return tempAvarage / MEASUREMENT_REPEATS;
}

/**
 * Get current humidity value from DTH sensor.
 */
float getHumidity() {
  float humiAvarage = 0;
  int index = 0;
  while (index != MEASUREMENT_REPEATS) {
    float humi = dht.readHumidity();
    if (humi != humi) { //is NaN
      delay(10);
      continue;
    }
    humiAvarage += humi;
    index++;
    delay(10);
  }
  return humiAvarage / MEASUREMENT_REPEATS;
}

/**
 * Get name of file generated from actual date.
 * For example '20180908.DAT' represents 8th September 2018. 
 */
char* getFileName() {
  char* name = (char*) calloc(8 + strlen(FILE_EXTENSION), sizeof(char));
  sprintf(name, "%d%02d%02d%s", year(), month(), day(), FILE_EXTENSION);
  return name;
}

/**
 * Generate string representing measurements data.
 * String is in followin form: 'FLAG_TYPE''HEX_VALUE''SEPARATOR'xN, where
 * FLAG_TYPE is a byte value determining measurement type,
 * HEX_VALUE is a hexadecimal 4 bytes determining measurement value,
 * SEPARATOR is a character separating different measurement values.
 */
char* createFileData(long timestamp) {
  float temperature = getTemperature();
  float humidity = getHumidity();

  char* string = (char*) calloc(3*(2+8)+2+1, sizeof(char)); //values_count * (flag + value) + separators_count + terminating_character
  sprintf(string, "%02X%08lX|%02X%08lX|%02X%08lX", RESPONSE_FLAG_TIMESTAMP, bigEndianLong(timestamp), RESPONSE_FLAG_TEMPERATURE, bigEndianFloat(temperature), RESPONSE_FLAG_HUMIDITY, bigEndianFloat(humidity));

  return string;
}

/**
 * Send actual data to bluetooth smart module.
 * Data are divided into individual packets because of ble limits.
 * Each packet consists of FLAG_TYPE and VALUE.
 */
void sendActualData() {
  //prepare timestamp packet
  long timestamp = getTimestamp();
  byte packet1[sizeof(timestamp) + 1];
  packet1[0] = RESPONSE_FLAG_TIMESTAMP;
  memcpy(packet1 + 1, &timestamp, sizeof(timestamp));

  //prepare temperature packet
  float temperature = getTemperature();
  byte packet2[sizeof(temperature) + 1];
  packet2[0] = RESPONSE_FLAG_TEMPERATURE;
  memcpy(packet2 + 1, &temperature, sizeof(temperature));

  //prepare humidity packet
  float humidity = getHumidity();
  byte packet3[sizeof(humidity) + 1];
  packet3[0] = RESPONSE_FLAG_HUMIDITY;
  memcpy(packet3 + 1, &humidity, sizeof(humidity));

  //send packets
  writeDataToBle(packet1, sizeof(timestamp) + 1);
  writeDataToBle(packet2, sizeof(temperature) + 1);
  writeDataToBle(packet3, sizeof(humidity) + 1);
  writeDataToBle(RESPONSE_FLAG_END, strlen(RESPONSE_FLAG_END));
}

/**
 * Load and send list of all file names from SD card to bluetooth module.
 */
void sendListOfFiles() {
  File root = SD.open("/", FILE_READ);  //root
  if (root) {
    while (true) {
      File file = root.openNextFile();
      if (!file) {
        break; //no more files
      } else if (file.isDirectory()) {
        // continue
      } else {
        //extract file name without extension
        char extractedName[8];
        memcpy(extractedName, &file.name()[0], 8);

        //prepare packet in form name(4 bytes) + size(4 bytes)
        long name = atol(extractedName);
        long size = file.size();
        byte packet[sizeof(name) + sizeof(size)];
        memcpy(packet, &name, sizeof(name));
        memcpy(packet + sizeof(name), &size, sizeof(size));
        
        writeDataToBle(packet, sizeof(name) + sizeof(size));
      }
      file.close();
    }
    root.close();
  }

  writeDataToBle(RESPONSE_FLAG_END, strlen(RESPONSE_FLAG_END));
}

/**
 * Load and send all measurement data from SD card to bluetooth module for given date.
 * Data are divided into individual packets because of ble limits.
 * Each packet consists of FLAG_TYPE and VALUE.
 */
void sendHistory(char* date) {
  File file = SD.open(date, FILE_READ);
  if (file) {
    byte packet[5];
    int index = 0;
    while (file.available()) {
      char character = file.read();
      if (character == '|' || character == '\n' || character == '\r') {
        index = 0;
      } else {
        char singleByte[2];
        singleByte[0] = character;
        char bytePart = file.read();
        singleByte[1] = bytePart;

        packet[index++] = (byte) strtoul(singleByte, NULL, 16);
      }

      if (index == 5) {
        writeDataToBle(packet, 5);
      }
    }

    file.close();
  }

  writeDataToBle(RESPONSE_FLAG_END, strlen(RESPONSE_FLAG_END));
}

/*
 * Read data from bluetooth smart module.
 * Return structure which contains array of bytes and size of array.
 */
BleData readDataFromBle() {
  if (Serial1.available()) {
    byte* data = (byte*) calloc(50, sizeof(byte));
    int size = 0;
    while (size < 50 && Serial1.available()) {
      data[size++] = Serial1.read();
      delay(50);  //ble module sends data one by one so wait is neccessary to get them as array
    }

    data = (byte*) realloc(data, size * sizeof(byte));
    return BleData{size, data};
  }
  return BleData{0, NULL};
}

/**
 * Write byte array to bluetooth smart module.
 */
void writeDataToBle(byte* data, int size) {
  Serial1.write(data, size);
  delay(50);
}

/**
 * Send sleep command to bluetooth module.
 */
void enterBleSleepMode() {
  Serial1.write(AT_SLEEP, strlen(AT_SLEEP));
  isSleeping = true;
}

