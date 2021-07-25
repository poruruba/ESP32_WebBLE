#include <M5StickC.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BLE2902.h"

#define UUID_SERVICE "08030900-7d3b-4ebf-94e9-18abc4cebede"
#define UUID_WRITE "08030901-7d3b-4ebf-94e9-18abc4cebede"
#define UUID_READ "08030902-7d3b-4ebf-94e9-18abc4cebede"
#define UUID_NOTIFY "08030903-7d3b-4ebf-94e9-18abc4cebede"

BLECharacteristic *pCharacteristic_write;
BLECharacteristic *pCharacteristic_read;
BLECharacteristic *pCharacteristic_notify;
BLEAdvertising *pAdvertising;

bool connected = false;

class MyCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){
    connected = true;
    Serial.println("Connected\n");
  }
  void onDisconnect(BLEServer* pServer){
    connected = false;
    BLE2902* desc = (BLE2902*)pCharacteristic_notify->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(false);
    Serial.println("Disconnected\n");
    
    pAdvertising->start();
  }
};

#define UUID_VALUE_SIZE 20
uint8_t value_write[UUID_VALUE_SIZE] = { 0 };
uint8_t value_read[] = { (uint8_t)((UUID_VALUE_SIZE >> 8) & 0xff), (uint8_t)(UUID_VALUE_SIZE & 0xff) };

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* pCharacteristic){
    Serial.println("onWrite");
    uint8_t* value = pCharacteristic->getData();
    std::string str = pCharacteristic->getValue(); 

    int len = str.length();
    memmove(value_write, value, len > UUID_VALUE_SIZE ? UUID_VALUE_SIZE : len);
  }
};

void taskServer(void*) {
  BLEDevice::init("M5Stick-C");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyCallbacks());
  BLEService *pService = pServer->createService(UUID_SERVICE);
  pCharacteristic_write = pService->createCharacteristic( UUID_WRITE, BLECharacteristic::PROPERTY_WRITE );
  pCharacteristic_write->setAccessPermissions(ESP_GATT_PERM_WRITE);
  pCharacteristic_write->setValue(value_write, sizeof(value_write));
  pCharacteristic_write->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic_read = pService->createCharacteristic( UUID_READ, BLECharacteristic::PROPERTY_READ );
  pCharacteristic_read->setAccessPermissions(ESP_GATT_PERM_READ);
  pCharacteristic_read->setValue(value_read, sizeof(value_read));
  pCharacteristic_notify = pService->createCharacteristic( UUID_NOTIFY, BLECharacteristic::PROPERTY_NOTIFY );
  pCharacteristic_notify->addDescriptor(new BLE2902());
  pService->start();
  pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(UUID_SERVICE);
  pAdvertising->start();
  vTaskDelay(portMAX_DELAY); //delay(portMAX_DELAY);
}

void setup() {
  // put your setup code here, to run once:
  M5.begin();

  Serial.begin(115200);
  Serial.println("setup");

  xTaskCreate(taskServer, "server", 20000, NULL, 5, NULL);
}

void loop() {
  // put your main code here, to run repeatedly:
  M5.update();
  
  if ( M5.BtnA.wasPressed() ){
    if( connected ){
      BLE2902* desc = (BLE2902*)pCharacteristic_notify->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
      if( !desc->getNotifications() )
        return;

      pCharacteristic_notify->setValue(value_write, sizeof(value_write));
      pCharacteristic_notify->notify();
    }
  }

  delay(1);
}