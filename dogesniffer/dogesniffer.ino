#include <PubSubClient.h>
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

const char *SSID = "PUT SSID HERE";
const char *PWD = "PUT PASSWORD HERE";
const char *googleApiKey = "PUT GOOGLE API KEY HERE";

WifiLocation location(googleApiKey);

// The remote service we wish to connect to.
static BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
// The characteristic of the remote service we are interested in.
static BLEUUID changeChar("00010203-0405-0607-0809-0a0b0c0d2b11");

static String bleDevice = "";
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;
uint8_t color[] = {0x33, 0x05, 0x02, 0x3a, 0x57, 0x7f, 0x00, 0xff, 0x98, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68}; // color
uint8_t poweroff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32}; // off
uint8_t poweron[] = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33}; // on
uint8_t* hexCode = new uint8_t[20];

// MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
const char *mqttServer = "broker.hivemq.com";
int mqttPort = 1883;

void setupMQTT() {
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
}

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.print(SSID);
  Serial.print("...");
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(SSID, PWD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("Connected!");
}

void getCoords() {
  location_t loc = location.getGeoFromWiFi();
  Serial.println("Location request data...");
  Serial.println("Latitude: " + String(loc.lat, 7));
  Serial.println("Longitude: " + String(loc.lon, 7));
  Serial.println("Accuracy: " + String(loc.accuracy));
  mqttClient.publish("dogesniffer/latitude", String(loc.lat, 7).c_str());
  mqttClient.publish("dogesniffer/longitude", String(loc.lon, 7).c_str());
  mqttClient.publish("dogesniffer/accuracy", String(loc.accuracy).c_str());
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
      return;
    }
    void onDisconnect(BLEClient* pclient) {
      connected = false;
      Serial.println("onDisconnect");
    }
};

/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class scanAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());
      Serial.println(advertisedDevice.getName().c_str());
      if (String(advertisedDevice.getName().c_str()) != "") {
        mqttClient.publish("dogesniffer/ble_devices", advertisedDevice.getName().c_str());
      }
      //send device name back to python ^^ ->only add unique names (do this on python side)
    } // onResult
}; // MyAdvertisedDeviceCallbacks

/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class findAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.getName().find(bleDevice.c_str()) != -1) { //replace with
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;
        Serial.println("BLE Device Connected!");
      } // Found our server
    } // onResult
}; // MyAdvertisedDeviceCallbacks

void scanBLE() {
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new scanAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void findBLE() {
  BLEDevice::init("");
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new findAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void writeBLE() {
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    //doConnect = false;
  }
}

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" --> Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);//BLEAddress(myDevice->getAddress()));  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" --> Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" --> Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(changeChar);

  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(changeChar.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" --> Found the Characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canWriteNoResponse())
    Serial.println ("Writing");
  for (int i = 0; i < 20; i++) {
    Serial.print(hexCode[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  pRemoteCharacteristic->writeValue(hexCode, 20);

  connected = true;
  return true;
}

void callback(char* topic, byte* payload, unsigned int length) {
  char received [50];
  for (int i = 0; i < length; i++) {
    received[i] = (char)payload[i];
  }
  String received_payload = String(received);
  if (strcmp(topic, "dogesniffer/wifi_selected") == 0) {
    if (received_payload.substring(0, 13) == "WiFi Selected") {
      mqttClient.publish("dogesniffer/wifi_status", "WiFi Confirmed!");
    }
  } else if (strcmp(topic, "dogesniffer/ble_selected") == 0) {
    if (received_payload.substring(0, 12) == "BLE Selected") {
      Serial.println("BLE Selected");
      Serial.println("Scanning for devices...");
      mqttClient.publish("dogesniffer/ble_status", "BLE Confirmed!");
      scanBLE();
    }
  } else if (strcmp(topic, "dogesniffer/refresh_devices") == 0) {
    if (received_payload.substring(0, 7) == "refresh") {
      Serial.println("Sending Mac Addresses!");
      String mac_address = location.getSurroundingWiFiJson();
      int n = mac_address.indexOf("macAddress");
      Serial.println(mac_address);
      while (n != -1) {
        mqttClient.publish("dogesniffer/device_names", mac_address.substring(n + 13, n + 30).c_str());
        n = mac_address.indexOf("macAddress", n + 30);
      }
    }
  } else if (strcmp(topic, "dogesniffer/BLE_device_chosen") == 0) {
    Serial.println("BLE Device Chosen!");
    if (received_payload.indexOf("Minger") != -1) {
      bleDevice = received_payload.substring(0, 12);
      findBLE();
      mqttClient.publish("dogesniffer/ble_lamp_init", "Device Found!");
    } else if (received_payload.indexOf("Govee") != -1) {
      bleDevice = received_payload.substring(0, 11);
      findBLE();
      mqttClient.publish("dogesniffer/ble_lamp_init", "Device Found!");
    } else {
      bleDevice = "";
      mqttClient.publish("dogesniffer/ble_lamp_init", "Fail!");
    }
  } else if (strcmp(topic, "dogesniffer/lamp_switch") == 0) {
    if (received_payload.substring(0, 1) == "1") {
      memcpy(hexCode, poweron, sizeof poweron);
      writeBLE();
      Serial.println("Lamp Switch Turned ON");
    } else {
      memcpy(hexCode, poweroff, sizeof poweroff);
      writeBLE();
      Serial.println("Lamp Switch Turned OFF");
    }
  } else if (strcmp(topic, "dogesniffer/get_location") == 0) {
    if (received_payload.substring(0, 10) == "Send Nudes") {
      getCoords();
    }
  } else if (strcmp(topic, "dogesniffer/hex_color") == 0) {
    uint8_t hexNumberByte[3];
    unsigned long number = strtoul(received_payload.c_str(), NULL, 16);
    for (int i = 2; i >= 0; i--) {
      hexNumberByte[i] = number & 0xFF;
      number >>= 8;
    }
    uint8_t xor_byte = 0x7a ^ hexNumberByte[0] ^ hexNumberByte[1] ^ hexNumberByte[2];
    for (int i = 0; i < 3; i++) {
      Serial.print(hexNumberByte[i], HEX);
      Serial.print(" ");
    }
    Serial.println(xor_byte);
    memcpy(hexCode, color, sizeof color);
    hexCode[3] = hexNumberByte[0];
    hexCode[4] = hexNumberByte[1];
    hexCode[5] = hexNumberByte[2];
    hexCode[19] = xor_byte;
    writeBLE();
    Serial.println("Lamp Switched Colors!");
  }
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Connected!");
      // Subscribe to topic
      mqttClient.subscribe("dogesniffer/#");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in a second...");
      delay(1000);
    }
  }
}

// The setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin 5 as an output.
  Serial.begin(115200);
  connectToWiFi();
  delay(500);
  setupMQTT();
}

// the loop function runs over and over again forever
void loop() {
  if (!mqttClient.connected())
    reconnect();

  mqttClient.loop();
}
