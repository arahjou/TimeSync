#include <Wire.h>
#include <Adafruit_AS7341.h>
#include <SPI.h>
#include <SD.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLECharacteristic.h>
#include "time.h"

// --- BLE Configuration ---
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0" // Use a unique UUID
#define CHARACTERISTIC_UUID "12345678-1234-5678-1234-56789abcdef1" // Use a unique UUID

// --- I2C Multiplexer and Sensor Configuration ---
#define TCA9548A_ADDRESS 0x70  // Default I2C address for TCA9548A
#define CS_PIN 5              // Chip select pin for SD card module

const int AS7341_CHANNELS[] = {0, 1, 2, 3, 4}; // Channels where AS7341s are connected
const int NUM_SENSORS = sizeof(AS7341_CHANNELS) / sizeof(AS7341_CHANNELS[0]);

// --- Buffering Configuration ---
const int BUFFER_SIZE = 10; // Number of records to buffer before writing to SD

// --- Global Variables ---
Adafruit_AS7341 as7341;
bool timeSet = false;      // Flag to indicate if time has been set via BLE
String dataBuffer = "";    // Buffer to hold data records
int bufferCount = 0;     // Counter for records in the buffer

// Define FILE_APPEND if not defined.
#ifndef FILE_APPEND
#define FILE_APPEND (O_CREAT | O_WRITE | O_APPEND)
#endif

// --- Function Declarations ---
void tca_select(uint8_t channel);
bool setTimeFromString(const String& timeMsg);
String getFormattedTime();
void writeBufferToSD(); // Forward declaration

// --- BLE Callback Class ---
class TimeCharacteristicCallbacks : public BLECharacteristicCallbacks {
public: // Added public access specifier
  void onWrite(BLECharacteristic* pCharacteristic) {
    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0) {
      Serial.print("Received BLE data: ");
      Serial.println(rxValue);

      if (setTimeFromString(rxValue)) {
        timeSet = true;
      }
    }
  }
};

// --- I2C Multiplexer Control ---
void tca_select(uint8_t channel) {
  if (channel > 7 && channel != 255) return; // Validate channel
  Wire.beginTransmission(TCA9548A_ADDRESS);
  if (channel == 255){
      Wire.write(0); // Deselect all channels
  }
  else{
    Wire.write(1 << channel);
  }
  if (Wire.endTransmission() != 0) { // Check for I2C errors
      Serial.println("I2C error during channel selection.");
  }
}

// --- Time Setting from String ---
bool setTimeFromString(const String& timeMsg) {
    // Example expected format: "2025-02-26 14:15:30|CET-1CEST,M3.5.0/2,M10.5.0/3"
    int splitIndex = timeMsg.indexOf('|');
    if (splitIndex < 0) {
        Serial.println("Error: No '|' delimiter in time string.");
        return false;
    }

    String timePart = timeMsg.substring(0, splitIndex);
    String tzPart   = timeMsg.substring(splitIndex + 1);

    // We expect timePart in "YYYY-MM-DD HH:MM:SS" format
    int year     = timePart.substring(0,4).toInt();
    int month    = timePart.substring(5,7).toInt();
    int day      = timePart.substring(8,10).toInt();
    int hour     = timePart.substring(11,13).toInt();
    int minute   = timePart.substring(14,16).toInt();
    int second   = timePart.substring(17,19).toInt();

  // Input validation: Check for reasonable values before proceeding
  if (year < 2023 || year > 2050 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    Serial.println("Error: Invalid date/time values.");
    return false;
  }

    // Set environment variable for timezone
    setenv("TZ", tzPart.c_str(), 1);
    tzset();

    // Build a tm struct
    struct tm t;
    t.tm_year = year - 1900;   // tm_year counts from 1900
    t.tm_mon  = month - 1;    // tm_mon is 0-based
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;
    t.tm_isdst = -1;          // Auto-detect DST

    time_t epoch = mktime(&t);
    if (epoch < 0) {
        Serial.println("Error: Could not convert time string to epoch.");
        return false;
    }

    // Apply the new time
    struct timeval now = { .tv_sec = epoch };
    settimeofday(&now, NULL);

    Serial.println("Time successfully set from BLE!");
    return true;
}

// --- Get Formatted Time ---
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "1970-01-01 00:00:00"; // Return a default time
  }
  char timeStringBuff[50]; // 50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

// --- Write Buffer to SD Card ---
void writeBufferToSD() {
  File file = SD.open("/sensor_data.csv", FILE_APPEND);
  if (file) {
    file.print(dataBuffer);
    file.close();
    Serial.println("Buffered data written to SD card.");
  } else {
    Serial.println("Failed to open file for writing buffered data.");
    // Consider adding an LED indicator or other error signal here
  }

  // Clear buffer
  dataBuffer = "";
  bufferCount = 0;
}


// --- Setup ---
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // --- Initialize BLE ---
  BLEDevice::init("AS7341_ESP32_BLE"); // Device Name
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ // Allow writing (for time)
  );
  pCharacteristic->setCallbacks(new TimeCharacteristicCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  Serial.println("BLE service is up and advertising.");

  // --- Initialize SD Card ---
  Serial.println("Initializing SD card...");
  if (!SD.begin(CS_PIN)) {
    Serial.println("Card Mount Failed");
    while (1); // Halt if SD card initialization fails
  }
  Serial.println("SD Card initialized.");

  // Check if file exists, create headers if needed
  if (!SD.exists("/sensor_data.csv")) {
    File file = SD.open("/sensor_data.csv", FILE_WRITE);
    if (file) {
      file.println("DateTime,SensorNumber,F1 (415nm),F2 (445nm),F3 (480nm),F4 (515nm),F5 (555nm),F6 (590nm),F7 (630nm),F8 (680nm),Clear,NIR");
      file.close();
      Serial.println("File created and headers written successfully.");
    } else {
      Serial.println("Failed to create file for writing headers."); // More specific error
    }
  } else {
    Serial.println("File already exists. Data will be appended.");
  }
    Serial.println("Waiting for BLE time sync..."); // Indicate waiting for time sync
}

// --- Main Loop ---
void loop() {
    if (timeSet) {
        // For each AS7341 sensor
        for (int i = 0; i < NUM_SENSORS; i++) {
            int channel = AS7341_CHANNELS[i];

            // Select the channel
            tca_select(channel);
            delay(10); // Short delay after selecting channel

            // Re-initialize the sensor – Potentially improve this
            if (!as7341.begin()) {
                Serial.print("AS7341 initialization failed on channel ");
                Serial.println(channel);
                continue;  // Skip to the next sensor if init fails
            }

            // Set sensor settings.  Consider making these configurable
            as7341.setATIME(100);
            as7341.setASTEP(999);
            as7341.setGain(AS7341_GAIN_256X);

            // Wait for at least integration time
            delay(200); // Could be optimized

            // Read data
            uint16_t readings[10] = {0};
            if (!as7341.readAllChannels(readings)) {
                Serial.print("Error reading all channels from AS7341 on channel ");
                Serial.println(channel);
            } else {
              // Get the current date and time
                String dateTime = getFormattedTime();

                // Create a data line for the CSV
                String dataLine = dateTime + "," + String(i + 1) + ",";
                for (int j = 0; j < 10; j++) {
                    dataLine += String(readings[j]);
                    if (j < 9) dataLine += ",";
                }
                dataLine += "\n";

                // Add data line to buffer
                dataBuffer += dataLine;
                bufferCount++;

                // Check if buffer is full
                if (bufferCount >= BUFFER_SIZE) {
                    writeBufferToSD();
                }
            }
            // Short delay between sensors.
            delay(100);
        }

        // Deselect all channels after each full sensor cycle
        tca_select(255);

        // Delay before next full cycle
        delay(1000); // Adjust this delay as needed

    } else {
        // Wait for time sync.  Avoid tight loop.
        delay(2000);
    }
}