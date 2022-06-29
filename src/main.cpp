#define ver "v0.1" 
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>

#define doorSwitch  D1

// define 跳板 AP SSID/PWD
#define bridge_ssid     "abc"       // 跳板 AP SSID
#define bridge_password "12345678"  // 跳板 AP PWD

// define apAP, API URLs
#define apAPI           "https://deploy-ap-api.herokuapp.com/?getAP"
#define doorOpenAPI     "https://maker.ifttt.com/trigger/MelodyFrontDoor/with/key/m_lrA1K46r_UGtYAdL4CyxqFYdRH3Zz60uGrg-1aUzb?value1=%E6%89%93%E9%96%8B"
#define doorCloseAPI    "https://maker.ifttt.com/trigger/MelodyFrontDoor/with/key/m_lrA1K46r_UGtYAdL4CyxqFYdRH3Zz60uGrg-1aUzb?value1=%E9%97%9C%E9%96%89"

String apiHttpsGet(const char* apiURL);
void   writeStringToEEPROM(int addrOffset, const String &strToWrite);
String readStringFromEEPROM(int addrOffset);
int    checkApAPI(int timeoutTick);
void(* resetFunc) (void) = 0; //declare reset function @ address 0

String aSSID="";                   // 場域 AP SSID
String aPWD="";                    // 場域 AP PWD

char json_input[100];
DeserializationError json_error;
const char* json_element;
StaticJsonDocument<200> json_doc; 
String apiReturn; 

int prevDoorState, doorState;

void setup() {
  pinMode(D1, INPUT_PULLUP);
    
  // Init Serial
  Serial.begin(115200);
  Serial.println();
  Serial.println("Source: MAQ D:\\WebApp Projects\\ArduinoDevices\\HomeSafty\\door_IFTTT_LINE\\");  
  Serial.println();

  Serial.println(WiFi.macAddress());

  // Check EEPROM
  EEPROM.begin(4096); 
  byte eepromFlag;                // eeprom Flag: 十六進制 55 表示有效
  eepromFlag = EEPROM.read(0); 

  // if eepromFlag !=0x55, connect to the bridge AP, "abc"/"12345678"
  // set mobile phone’s hotspot as "abc"/"12345678"
  if (eepromFlag !=0x55) {
    Serial.print("No SSID in EEPROM, Connect to bridge_AP: ");
    Serial.println(bridge_ssid); 
   
    if (checkApAPI(50)==0){
      EEPROM.write(0,0x55);
      writeStringToEEPROM(1, aSSID);
      writeStringToEEPROM(1+aSSID.length()+1, aPWD);  
      EEPROM.commit();       
      WiFi.disconnect();
    } else {
      resetFunc(); //reset
    }   
  } else { // if eepromFlag==55, read aSSID/aPWD from EEPROM
    Serial.println("Check bridge API for update EEPROM");

    if (checkApAPI(20)!=0){
      Serial.println("Read SSID/PWD from EEPROM");
      aSSID =  readStringFromEEPROM(1);
      aPWD =  readStringFromEEPROM(1+aSSID.length()+1); 
    } else {
      EEPROM.write(0,0x55);
      writeStringToEEPROM(1, aSSID);
      writeStringToEEPROM(1+aSSID.length()+1, aPWD);  
      EEPROM.commit();       
      WiFi.disconnect();      
    }
  }
   
  //WiFi.mode(WIFI_STA); // 本來以為一定要執行，但好像預設就是 Station mode

  WiFi.begin(aSSID, aPWD);
  Serial.printf("Connecting to AP/Router %s...\n", aSSID.c_str()); 

  wifi_set_sleep_type(NONE_SLEEP_T); // 不要讓 WiFi 做 sleep  

  int iTimeout=0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.printf("%d.", iTimeout);
    if (iTimeout++==50) {
      resetFunc();    
      break;
    }
  }

  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // Check ifttt maker webhook API
  Serial.println(doorCloseAPI);
  apiReturn = apiHttpsGet(doorCloseAPI);
  Serial.println(apiReturn);

  Serial.println();  
  Serial.println(F("Ready! checking door switch...."));  

  prevDoorState = digitalRead(doorSwitch);  
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    //read the door switch pin: 
    doorState = digitalRead(doorSwitch);

    if (doorState != prevDoorState) {
      Serial.print("Door state change from ");
      Serial.print(prevDoorState);
      Serial.print(" to ");
      Serial.println(doorState);     

      if (doorState==0) 
        apiReturn = apiHttpsGet(doorCloseAPI);
      else
        apiReturn = apiHttpsGet(doorOpenAPI);

      prevDoorState = doorState;
    }

    delay(100);        // delay in between reads for stability

  } else {
    Serial.println("Networked is not connected");
    resetFunc(); //reset
  }
}    

String apiHttpsGet(const char* apiURL){  

  BearSSL::WiFiClientSecure httpsGetClient;
  HTTPClient httpsGet;

  httpsGetClient.setInsecure(); 
  httpsGet.setTimeout(20000);   

  String payload="";
  
  Serial.print("[HTTPS] begin...\n");
  if (httpsGet.begin(httpsGetClient, apiURL)) { 
  //if (https.begin(httpsClient, apiURL)) { 

    Serial.print("[HTTPS] GET...\n");
    // start connection and send HTTP header
    unsigned long lastTime=millis();
    int httpCode = httpsGet.GET();
    Serial.print("httpsGet: ");
    Serial.println(millis()-lastTime);

    Serial.println(httpCode);
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {        
        payload = httpsGet.getString();          
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", httpsGet.errorToString(httpCode).c_str());
      payload="";
    }

    httpsGet.end();
    //return payload;
  }  
  return payload;
}

// EEPROM write/read string routines
void writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

String readStringFromEEPROM(int addrOffset)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0'; // !!! NOTE !!! Remove the space between the slash "/" and "0" (I've added a space because otherwise there is a display bug)
  return String(data);
}
// End EEPROM write/read string routines

int checkApAPI(int timeoutTick){            
  // Connect to the Bridge AP
  WiFi.mode(WIFI_STA);
  WiFi.begin(bridge_ssid, bridge_password);

  Serial.printf("Connecting to bridge hotspot AP %s...\n", bridge_ssid); 

  int iTimeout=0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.printf("%d.", iTimeout);
    if (iTimeout++==timeoutTick) {
      Serial.println();
      return -1; //timeout failure
    }
  }

  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  while (aSSID=="") {
    Serial.println("Get apAPI");
    apiReturn = apiHttpsGet(apAPI);  
    Serial.println(apiReturn);
    apiReturn.toCharArray(json_input,100);       
    json_error = deserializeJson(json_doc, json_input);
    if (!json_error) {
      json_element = json_doc["SSID"];
      aSSID = String(json_element);  
      json_element = json_doc["PWD"];
      aPWD = String(json_element);  

      // EEPROM.write(0,0x55);
      // writeStringToEEPROM(1, aSSID);
      // writeStringToEEPROM(1+aSSID.length()+1, aPWD);  
      // EEPROM.commit();                           
    } else return -2; //json error  
  }
  return 0; //success  
}
