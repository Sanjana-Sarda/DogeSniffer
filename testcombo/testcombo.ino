#include <Arduino.h>
#include <WiFi.h>
#include <WifiLocation.h>
#include "BLEDevice.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

const char* googleApiKey = "YOUR_GOOGLE_API_KEY";
const char* ssid = "SSID";
const char* passwd = "PASSWD";

WifiLocation location(googleApiKey);

// The remote service we wish to connect to.
static BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
// The characteristic of the remote service we are interested in.
static BLEUUID changeChar("00010203-0405-0607-0809-0a0b0c0d2b11");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;
uint8_t color[] = {0x33, 0x05, 0x02, 0x3a, 0x57, 0x7f, 0x00, 0xff, 0x98, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68}; // color
uint8_t poweroff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32}; // off
uint8_t poweron[] = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33}; // on

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    Serial.println(advertisedDevice.getName().c_str());
    //send device name back to python ^^ ->only add unique names (do this on python side)

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.getName()=="Minger_H6001_0751") {//replace with 
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
      

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void getCoords() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(ssid, passwd);
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print("Attempting to connect to WPA SSID: ");
      Serial.println(ssid);
      // wait 5 seconds for connection:
      Serial.print("Status = ");
      Serial.println(WiFi.status());
      delay(500);
   }
   location_t loc = location.getGeoFromWiFi();

   Serial.println("Location request data");
   Serial.println(location.getSurroundingWiFiJson());
   Serial.println("Latitude: " + String(loc.lat, 7));
   Serial.println("Longitude: " + String(loc.lon, 7));
   Serial.println("Accuracy: " + String(loc.accuracy));
}

void scanBLE(){
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}
  

void writeBLE(){
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    doConnect = false;
  }
}



bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);//BLEAddress(myDevice->getAddress()));  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(changeChar);
    
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(changeChar.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found the Characteristic");


    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canWriteNoResponse())
      Serial.println ("Writing");
      pRemoteCharacteristic->writeValue(poweron, 20); 

    connected = true;
    return true;
}



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial);
  getCoords();
  scanBLE();
  writeBLE();
}

void loop() {
  // put your main code here, to run repeatedly:

}
