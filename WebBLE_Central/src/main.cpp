#include <M5StickC.h>
#define LGFX_AUTODETECT // 自動認識
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <ArduinoJson.h>

const char* wifi_ssid = "【WiFiアクセスポイントのSSID】";
const char* wifi_password = "【WiFiアクセスポイントのパスワード】";

#define WEBSOCKET_PORT  81 // WebSocketのポート番号
#define DURATION_DEFAULT  5

// HTTP POST(JSON)のリクエスト用ArduinoJson変数
const int capacity_request = JSON_OBJECT_SIZE(256);
StaticJsonDocument<capacity_request> json_request;
// HTTP POST/GETのレスポンス用ArduinoJson変数
const int capacity_response = JSON_OBJECT_SIZE(256);
StaticJsonDocument<capacity_response> json_response;
const int capacity_error = JSON_OBJECT_SIZE(20);
StaticJsonDocument<capacity_error> json_error;
// ArduinoJsonのパース用バッファ
char json_buffer[2048];
// テンポラリバッファ
uint8_t temp_buffer[1024];

WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);
static LGFX lcd;
BLEUUID targetServiceUUID;
bool allPrimary;
int ws_num = -1;
bool is_connected = false;
BLEClient *client;

std::map<std::string, BLEAdvertisedDevice *> g_foundDevices;
std::map<std::string, BLERemoteService *> g_foundServices;
std::map<std::string, BLERemoteCharacteristic *> g_characteristics;

char toC(unsigned char bin)
{
  if (bin >= 0 && bin <= 9)
    return '0' + bin;
  if (bin >= 0x0a && bin <= 0x0f)
    return 'a' + bin - 10;
  return '0';
}

unsigned char tohex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'F' && c <= 'F')
    return c - 'A' + 10;

  return 0;
}

long parse_hexstr(const char *p_hex, unsigned char *p_bin)
{
  if( p_hex == NULL )
    return 0;

  int index = 0;
  while (p_hex[index * 2] != '\0')
  {
    p_bin[index] = tohex(p_hex[index * 2]) << 4;
    p_bin[index] |= tohex(p_hex[index * 2 + 1]);
    index++;
  }

  return index;
}

std::string create_hexstr(const unsigned char *p_bin, unsigned short len)
{
  if( p_bin == NULL )
    return NULL;

  std::string str = "";
  for (int i = 0; i < len; i++)
  {
    str += toC((p_bin[i] >> 4) & 0x0f);
    str += toC(p_bin[i] & 0x0f);
  }

  return str;
}

std::string create_btaddress(const uint8_t *address){
  if( address == NULL )
    return NULL;

    std::string str = "";
    for( int i = 0 ; i < 6 ; i++ ){
      if( i != 0 )
        str += ':';
      str += toC((address[i] >> 4) & 0x0f);
      str += toC(address[i] & 0x0f);
    }

    return str;
}

String wifi_connect(const char *ssid, const char *password)
{
  Serial.println("");
  Serial.print("WiFi Connenting");
  lcd.println("WiFi Connecting");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    lcd.print(".");
    delay(1000);
  }
  Serial.printf("\nConnected\n%s (%d)\n", WiFi.localIP().toString().c_str(), WEBSOCKET_PORT);
  lcd.printf("\nConnected\n%s (%d)\n", WiFi.localIP().toString().c_str(), WEBSOCKET_PORT);

  return WiFi.localIP().toString();
}

void sendError(const char* type, const char *message ){
  json_error.clear();
  json_error["status"] = "NG";
  json_error["result"]["type"] = type;
  json_error["result"]["message"] = message;

  size_t len = serializeJson(json_error, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return;

  if( ws_num >= 0 )
    webSocket.sendTXT(ws_num, json_buffer);
}

void sendErrorWithNum(int num, const char *message){
  json_error.clear();
  json_error["status"] = "NG";
  json_error["result"]["type"] = "socket";
  json_error["result"]["message"] = message;

  size_t len = serializeJson(json_error, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return;

  webSocket.sendTXT(num, json_buffer);
}

class advertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print("Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    std::string name = advertisedDevice.getAddress().toString();
    if (!g_foundDevices[name])
      g_foundDevices[name] = new BLEAdvertisedDevice(advertisedDevice);

    // 目的のBLEデバイスならスキャンを停止して接続準備をする
    if ( !allPrimary && advertisedDevice.haveServiceUUID()){
      BLEUUID service = advertisedDevice.getServiceUUID();
      Serial.printf("Have ServiceUUI: %s\n", service.toString().c_str());
      if (service.equals(targetServiceUUID)){
        BLEDevice::getScan()->stop();
      }
    }
  }
};

long bleScan(uint32_t duration, const char *serviceUuid){
  Serial.println("bleScan");

  g_foundDevices.clear();
  if( serviceUuid != NULL ){
    targetServiceUUID = BLEUUID::fromString(serviceUuid);
    allPrimary = false;
  }else{
    allPrimary = true;
  }

  Serial.println("Advertising start");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new advertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  BLEScanResults result = pBLEScan->start(duration);
  Serial.printf("completed %d\n", result.getCount());

  json_response.clear();
  json_response["status"] = "OK";
  json_response["result"]["type"] = "scan";

  for( auto itr = g_foundDevices.begin(); itr != g_foundDevices.end() ; itr++ ){
    std::string name = itr->first;
    BLEAdvertisedDevice *device = itr->second;
    Serial.printf("%s (%d) %s\n", name.c_str(), device->getAddressType(), device->getName().c_str());
    JsonObject obj = json_response["result"]["list"].createNestedObject();

    obj["address"] = (char *)name.c_str();
    obj["addresstype"] = device->getAddressType();

    if (device->haveName())
      obj["name"] = (char *)device->getName().c_str();
    if (device->haveTXPower())
      obj["txpower"] = device->getTXPower();
    if (device->haveRSSI())
      obj["rssi"] = device->getRSSI();
  }

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -1;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

long bleScanResult(const char *address){
  Serial.println("bleScanResult");

  BLEAdvertisedDevice *device = g_foundDevices[address];
  if (!device)
    return -1;

  json_response.clear();
  json_response["status"] = "OK";
  JsonObject obj = json_response["result"].to<JsonObject>();

  obj["type"] = "scanresult";
  obj["address"] = address;
  obj["addresstype"] = device->getAddressType();

  if (device->haveName())
    obj["name"] = device->getName().c_str();
  if (device->haveTXPower())
    obj["txpower"] = device->getTXPower();
  if (device->haveRSSI())
    obj["rssi"] = device->getRSSI();
  if (device->haveServiceUUID())
    obj["serviceuuid"] = device->getServiceUUID().toString().c_str();

  if( device->haveAppearance() )
    obj["appearance"] = device->getAppearance();
  if (device->haveManufacturerData())
    obj["manufacturerData"] = device->getManufacturerData().c_str();

  uint8_t *payload = device->getPayload();
  int payload_len = device->getPayloadLength();
  obj["payload"] = create_hexstr(payload, payload_len).c_str();

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -2;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

class clientCallbacks : public BLEClientCallbacks
{
  void onConnect(BLEClient *pClient){
    Serial.println("ble connected");

    is_connected = true;

    uint8_t macBT[6];
    esp_read_mac(macBT, ESP_MAC_BT);

    json_response.clear();
    json_response["status"] = "OK";
    json_response["result"]["type"] = "connect";
    json_response["result"]["local"] = (char *)create_btaddress(macBT).c_str();
    json_response["result"]["peer"] = (char *)pClient->getPeerAddress().toString().c_str();

    size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
    if (len < 0 || len >= sizeof(json_buffer))
      return;

    webSocket.sendTXT(ws_num, json_buffer);
  };

  void onDisconnect(BLEClient *pClient){
    Serial.println("ble disconnected");

    is_connected = false;

    uint8_t macBT[6];
    esp_read_mac(macBT, ESP_MAC_BT);

    json_response.clear();
    json_response["status"] = "OK";
    json_response["result"]["type"] = "disconnect";
    json_response["result"]["local"] = (char *)create_btaddress(macBT).c_str();
    json_response["result"]["peer"] = (char *)pClient->getPeerAddress().toString().c_str();

    size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
    if (len < 0 || len >= sizeof(json_buffer))
      return;

    webSocket.sendTXT(ws_num, json_buffer);
  }
};

long bleDisconnect(){
  Serial.println("bleDisconnect");

  if (client == NULL)
    return 0;

  client->disconnect();
  client = NULL;
  g_foundServices.clear();
  g_characteristics.clear();

  return 0;
}

long bleConnect(const char* address){
  Serial.println("bleConnect");

  if (client != NULL && client->isConnected())
    bleDisconnect();

  client = BLEDevice::createClient();
  client->setClientCallbacks(new clientCallbacks());
  Serial.println(" - Created client.");

  // ペリフェラルと接続して
  BLEAdvertisedDevice *device = g_foundDevices[address];
  if( !device ){
    sendError("connect", "not found");
    return -1;
  }

  if( !client->connect(device) ){
    sendError("connect", "cant connect");
    return -2;
  }

  Serial.println(" - Connected to peripheral.");

  return 0;
}

long bleConnect2(const char *address, esp_ble_addr_type_t type){
  Serial.println("bleConnect2");

  if (client != NULL && client->isConnected())
    bleDisconnect();

  client = BLEDevice::createClient();
  client->setClientCallbacks(new clientCallbacks());
  Serial.println(" - Created client.");

  BLEAddress bleaddr = BLEAddress(address);
  if( !client->connect(bleaddr, type) ){
    sendError("connect", "cant connect");
    return -2;
  }

  Serial.println(" - Connected to peripheral.");

  return 0;
}

long bleGetAllPrimaryServices(void){
  Serial.println("bleGetAllPrimaryServices");

  if (client == NULL){
    sendError("primary", "not connected");
    return -1;
  }

  std::map<std::string, BLERemoteService *> *foundServices = client->getServices();
  Serial.printf("services %d\n", foundServices->size());

  int i = 0;
  for( auto itr = foundServices->begin(); itr != foundServices->end() ; itr++ ){
    std::string name = itr->first;
    BLERemoteService *service = itr->second;
    Serial.printf("[%d] %s %s\n", i++, name.c_str(), service->getUUID().toString().c_str());
    if (!g_foundServices[name])
      g_foundServices[name] = service;
  }

  json_response.clear();
  json_response["status"] = "OK";
  json_response["result"]["type"] = "primary";

  for (auto itr = foundServices->begin(); itr != foundServices->end(); itr++){
    std::string name = itr->first;
    JsonObject obj = json_response["result"]["list"].createNestedObject();
    obj["uuid"] = (char *)name.c_str();
  }

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -1;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

long bleGetPrimaryService(const char *uuid){
  Serial.println("bleGetPrimaryService");

  if (client == NULL){
    sendError("service", "not connected");
    return -1;
  }

  BLERemoteService *service = client->getService(BLEUUID::fromString(uuid));
  if( service == NULL ){
    sendError("service", "not found");
    return -1;
  }

  std::string name = service->getUUID().toString();
  if (!g_foundServices[name])
    g_foundServices[name] = service;

  json_response.clear();
  json_response["status"] = "OK";
  json_response["result"]["type"] = "service";
  json_response["result"]["uuid"] = (char *)service->getUUID().toString().c_str();

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -1;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

long bleGetAllCharacteristics(const char* service_uuid){
  Serial.println("bleGetAllCharacteristics");

  if (client == NULL){
    sendError("chars", "not connected");
    return -1;
  }

  BLERemoteService *service = g_foundServices[service_uuid];
  if (service == NULL){
    sendError("chars", "not found");
    return -1;
  }

  Serial.printf("[%d] %s\n", service->getHandle(), service->getUUID().toString().c_str() );
  std::map<std::string, BLERemoteCharacteristic *> *characteristics = service->getCharacteristics();

  for( auto itr = characteristics->begin(); itr != characteristics->end() ; itr++ ){
    std::string name = itr->first;
    BLERemoteCharacteristic *characteristic = itr->second;
    Serial.printf("[%d] %s %s\n", characteristic->getHandle(), name.c_str(), characteristic->getUUID().toString().c_str());
    if ( !g_characteristics[name] )
      g_characteristics[name] = characteristic;
  }

  json_response.clear();
  json_response["status"] = "OK";
  json_response["result"]["type"] = "chars";

  for (auto itr = g_characteristics.begin(); itr != g_characteristics.end(); itr++){
    std::string name = itr->first;
    BLERemoteCharacteristic *characteristic = itr->second;
    JsonObject obj = json_response["result"]["list"].createNestedObject();
    obj["uuid"] = (char *)name.c_str();
    if( characteristic->canBroadcast() ) obj["properties"]["broadcast"] = true;
    if( characteristic->canRead() ) obj["properties"]["read"] = true;
    if( characteristic->canWriteNoResponse() ) obj["properties"]["writeWithoutResponse"] = true;
    if( characteristic->canWrite() ) obj["properties"]["write"] = true;
    if( characteristic->canNotify() ) obj["properties"]["notify"] = true;
    if( characteristic->canIndicate() ) obj["properties"]["indicate"] = true;
  }

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -1;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

long bleGetCharacteristic(const char* service_uuid, const char* uuid){
  Serial.println("bleGetCharacteristic");

  if (client == NULL){
    sendError("char", "not connected");
    return -1;
  }

  BLERemoteService *service = g_foundServices[service_uuid];
  if (service == NULL)
    return -1;

  BLERemoteCharacteristic *characteristic = service->getCharacteristic(BLEUUID::fromString(uuid));
  if( characteristic == NULL ){
    sendError("char", "not found");
    return -1;
  }

  std::string name = characteristic->getUUID().toString();
  if (!g_characteristics[name])
    g_characteristics[name] = characteristic;

  json_response.clear();
  json_response["status"] = "OK";

  json_response["result"]["type"] = "char";
  json_response["result"]["uuid"] = (char *)name.c_str();
  if( characteristic->canBroadcast() ) json_response["result"]["properties"]["broadcast"] = true;
  if( characteristic->canRead() ) json_response["result"]["properties"]["read"] = true;
  if( characteristic->canWriteNoResponse() ) json_response["result"]["properties"]["writeWithoutResponse"] = true;
  if( characteristic->canWrite() ) json_response["result"]["properties"]["write"] = true;
  if( characteristic->canNotify() ) json_response["result"]["properties"]["notify"] = true;
  if( characteristic->canIndicate() ) json_response["result"]["properties"]["indicate"] = true;

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return -1;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  Serial.printf("notifyCallback %s\n", pBLERemoteCharacteristic->getUUID().toString().c_str());

  json_response.clear();
  json_response["status"] = "OK";

  json_response["result"]["type"] = isNotify ? "notify" : "indicate";
  json_response["result"]["uuid"] = (char *)pBLERemoteCharacteristic->getUUID().toString().c_str();

  std::string datahex = create_hexstr(pData, length);
  json_response["result"]["data"] = (char *)datahex.c_str();

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
    return;

  webSocket.sendTXT(ws_num, json_buffer);
}

long bleStartNotification(const char *uuid){
  Serial.println("bleStartNotification");

  if (client == NULL){
    sendError("startnotify", "not connected");
    return -1;
  }

  BLERemoteCharacteristic *characteristic = g_characteristics[uuid];
  if (characteristic == NULL || (!characteristic->canNotify() && !characteristic->canIndicate()))
    return -1;

  characteristic->registerForNotify(notifyCallback, characteristic->canNotify());

  return 0;
}

long bleStopNotification(const char *uuid){
  Serial.println("bleStopNotification");

  if (client == NULL){
    sendError("stopnotify", "not connected");
    return -1;
  }

  BLERemoteCharacteristic *characteristic = g_characteristics[uuid];
  if (characteristic == NULL || (!characteristic->canNotify() && !characteristic->canIndicate()))
    return -1;

  characteristic->registerForNotify(NULL);

  return 0;
}

long bleWriteValue(const char* uuid, const uint8_t *p_value, uint16_t length, bool withresponse ){
  Serial.println("bleWriteValue");

  if (client == NULL){
    sendError("write", "not connected");
    return -1;
  }

  BLERemoteCharacteristic *characteristic = g_characteristics[uuid];
  if (withresponse ){
    if (characteristic == NULL || !characteristic->canWrite()){
      sendError("write", "cant write");
      return -2;
    }
  }else{
    if (characteristic == NULL || !characteristic->canWriteNoResponse()){
      sendError("write", "cant write");
      return -2;
    }
  }

  characteristic->writeValue((uint8_t *)p_value, length, withresponse);

  if( withresponse ){
    json_response.clear();
    json_response["status"] = "OK";
    json_response["result"]["type"] = "write";

    size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
    if (len < 0 || len >= sizeof(json_buffer))
      return -3;

    webSocket.sendTXT(ws_num, json_buffer);
  }

  return 0;
}

long bleReadValue(const char* uuid){
  Serial.println("bleReadValue");

  if (client == NULL){
    sendError("read", "not connected");
    return -1;
  }

  BLERemoteCharacteristic *characteristic = g_characteristics[uuid];
  if (characteristic == NULL || !characteristic->canRead()){
    sendError("read", "cant read");
    return -1;
  }

  std::string str = characteristic->readValue();
  int datalen = str.length();
  uint8_t *data = characteristic->readRawData();

  json_response.clear();
  json_response["status"] = "OK";

  json_response["result"]["type"] = "read";
  json_response["result"]["uuid"] = uuid;
  std::string datahex = create_hexstr(data, datalen);
  json_response["result"]["data"] = (char *)datahex.c_str();

  size_t len = serializeJson(json_response, json_buffer, sizeof(json_buffer));
  if (len < 0 || len >= sizeof(json_buffer))
     return -3;

  webSocket.sendTXT(ws_num, json_buffer);

  return 0;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      if( num == ws_num ){
        bleDisconnect();
        ws_num = -1;
      }
      break;
    case WStype_CONNECTED:{
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      if( ws_num >= 0 ){
        sendErrorWithNum(num, "already used");
        return;
      }

      ws_num = num;
      break;
    }
    case WStype_TEXT:{
      Serial.printf("WStype_TEXT : [%u] get Text: %s\n", num, payload);

      if (ws_num != num){
        sendErrorWithNum(num, "wrong num");
        return;
      }

      DeserializationError err = deserializeJson(json_request, payload, length);
      if( err ){
        Serial.println("Error: deserializeJson");
        return;
      }

      const char *type = json_request["type"];
      if (strcmp(type, "scan") == 0){
        const char *service_uuid = json_request["serviceuuid"];
        int duration = json_request["duration"];
        long ret = bleScan(duration <= 0 ? DURATION_DEFAULT : duration, service_uuid);
        Serial.printf("ret=%ld\n", ret);
      }else
      if (strcmp(type, "scanresult") == 0){
        const char *address = json_request["address"];
        long ret = bleScanResult(address);
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "connect") == 0 ){
        const char *address = json_request["address"];
        int addrtype = json_request["addresstype"] | -1;
        long ret;
        if( addrtype < 0 )
          ret = bleConnect(String(address).c_str());
        else
          ret = bleConnect2(String(address).c_str(), (esp_ble_addr_type_t)addrtype);
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "primary") == 0 ){
        long ret = bleGetAllPrimaryServices();
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "service") == 0 ){
        const char *service_uuid = json_request["serviceuuid"];
        long ret = bleGetPrimaryService(String(service_uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "chars") == 0 ){
        const char *service_uuid = json_request["serviceuuid"];
        long ret = bleGetAllCharacteristics(String(service_uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "char") == 0 ){
        const char *service_uuid = json_request["serviceuuid"];
        const char *uuid = json_request["uuid"];
        long ret = bleGetCharacteristic(String(service_uuid).c_str(), String(uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "read") == 0 ){
        const char *uuid = json_request["uuid"];
        long ret = bleReadValue(String(uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "write") == 0 ){
        const char *uuid = json_request["uuid"];
        const char *value = json_request["value"];
        bool withresponse = json_request["withresponse"];
        long declen = parse_hexstr(value, temp_buffer);
        long ret = bleWriteValue(String(uuid).c_str(), temp_buffer, declen, withresponse);
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "startnotify") == 0 ){
        const char *uuid = json_request["uuid"];
        long ret = bleStartNotification(String(uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "stopnotify") == 0 ){
        const char *uuid = json_request["uuid"];
        long ret = bleStopNotification(String(uuid).c_str());
        Serial.printf("ret=%ld\n", ret);
      }else
      if( strcmp(type, "disconnect") == 0 ){
        long ret = bleDisconnect();
        Serial.printf("ret=%ld\n", ret);
      }

      break;
    }
    case WStype_BIN:
      Serial.printf("WStype_BIN : [%u] get binary length: %u\n", num, length);
      break;
    case WStype_ERROR:
      Serial.printf("WStype_ERROR : [%u]\n", num);
      break;
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_FRAGMENT_TEXT_START:
    default:
      break;
  }
}

void setup() {
  // put your setup code here, to run once:
  M5.begin();
  Serial.begin(115200);
  Serial.println("setup");

  lcd.init();
  lcd.setRotation(3);

  wifi_connect(wifi_ssid, wifi_password);

  BLEDevice::init("M5StickC");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  M5.update();
  webSocket.loop();

  if( M5.BtnB.wasPressed() ){
    esp_restart();
  }
}