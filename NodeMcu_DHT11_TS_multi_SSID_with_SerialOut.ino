
#define USE_SECRET  1

// 1 "TP-Link_BB"    
// 2 "BB_Home2"  
// 3 "P20 Pro"    
// 4 "BB_guest"

//#define USE_TEST_CHANNEL
#define SKIP_TS_COMMUNICATION

#define VERSION                 "v2.4_sd"
#define BUILDNUM                      22

#define TEMPERATURE_CORRECTION   (-0.5f)

#define SERIAL_BOUND_RATE         115200
#define SOFT_SERIAL_BOUND_RATE      9600

// DHT-11 module powered by NodeMCU's 5V 
// DHT-11 connected to D5 of NodeMCU
// Pinout: https://circuits4you.com/2017/12/31/nodemcu-pinout/

//#include "DHTesp.h"

#include <SoftwareSerial.h>
#include <DHTesp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <ThingSpeak.h>


#include "secrets.h"
#include "params.h"
#include <EEPROM.h>


#define ON                         1
#define OFF                        0
 
DHTesp dht;

//==================================
// WIFI
//==================================
const char* ssid =           S_SSID;
const char* password =       S_PASS;



// =================================
// HTTP
// =================================
ESP8266WebServer server(SERVER_PORT);
HTTPClient http;


//==================================
// ThingSpeak
//==================================
WiFiClient client;
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;


//==================================
// SoftSerial
//==================================
SoftwareSerial serialOut(SOFT_SERIAL_RX, SOFT_SERIAL_TX);      // (Rx, Tx)

//==================================
// EEPROM
//==================================
int eepromAddr =                   0;
int eepromAddr2 =                 10;


//==================================
// "Data" variables
//==================================
float valC = NAN;
float valF = NAN;
float valH = NAN;
float valT = NAN;

int tempSet = 150; // means 15,0 °C

int overheatingDifference = 0;
short action = NOTHING; // can be NOTHING or HEATING

int lastTSUpdateStatus = -1;

long updateCount = 0;
String lastPhaseStatus = "";

//==================================
// Other variables
//==================================
long lastTemp = 0;      //time of the last measurement
long lastWiFiCheck = 0;
int errorCount = 0;

unsigned long elapsedTime = 0;


#ifndef SKIP_TS_COMMUNICATION
// ==========================================================================================================================
//                                                    ThingSpeak communication
// ==========================================================================================================================

int sendValuesToTSServer(float heatindex, float humidity, float temp1) {

  // ThingSpeak
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  ThingSpeak.setField(1, heatindex);
  ThingSpeak.setField(2, humidity);
  ThingSpeak.setField(3, temp1);

  // write to the ThingSpeak channel
  lastTSUpdateStatus = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  if(lastTSUpdateStatus == 200){
    Serial.println("Channel update successful.");
    ledBlink(2, 200, 100);
    return 1;
  }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(lastTSUpdateStatus));
    int errorCodeFirstDigit = (abs(lastTSUpdateStatus)/100);
    ledBlink(errorCodeFirstDigit, 50, 50);
    // Increase error count for restarting after defined max error counts..
    errorCount++;
    return 0;
  }
}

int sendValuesToTS(float heatindex, float humidity, float temp1){
  if (client.connect("api.thingspeak.com",80)){   //   "184.106.153.149" or api.thingspeak.com
        
       String postStr = SECRET_WRITE_APIKEY;
       postStr +="&field1=";
       postStr += String(heatindex);
       postStr +="&field2=";
       postStr += String(humidity);
       postStr +="&field3=";
       postStr += String(temp1);
       postStr += "\r\n\r\n";

       client.print("POST /update HTTP/1.1\n");
       client.print("Host: api.thingspeak.com\n");
       client.print("Connection: close\n");
       client.print("X-THINGSPEAKAPIKEY: " + String(SECRET_WRITE_APIKEY) +"\n");
       client.print("Content-Type: application/x-www-form-urlencoded\n");
       client.print("Content-Length: ");
       client.print(postStr.length());
       client.print("\n\n");
       client.print(postStr);
       Serial.println("Values sent to TS server..");

    }
    client.stop();
    delay(100);
    return 1;
}

#endif

// ==========================================================================================================================
//                                                 Hw feedback function
// ==========================================================================================================================

void turnLED(int state){
  if(state > 0){
    digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  }else{
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

void ledBlink(int blinkCount, int onTime, int offTime){
  for(short int i=0; i<blinkCount; i++){
    turnLED(ON);
    delay(onTime);                      // Wait for a second
    turnLED(OFF);
    if(i < blinkCount-1){
      delay(offTime);
    }
  }
}

// ==========================================================================================================================
//                                                     Other methods
// ==========================================================================================================================

void doRestart(){
  Serial.println("Restart now...");
  ESP.restart();
}


// ==========================================================================================================================
//                                                  Web server functions
// ==========================================================================================================================

void HandleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

String actionTempSet(){
  String value = server.arg("value"); //this lets you access a query param (http://x.x.x.x/set?value=12)
  tempSet = value.toInt();

  // save into eeprom
  EEPROM.write(eepromAddr, tempSet);
  EEPROM.commit();
  
  Serial.println("Try to SET: " + String(tempSet) );
  String m = String("Target temperature value set: ") + String(tempSet);
  return m;
}

String actionOverheatSet(){
  String value = server.arg("value"); //this lets you access a query param (http://x.x.x.x/set?value=12)
  overheatingDifference = value.toInt();

  // save into eeprom
  EEPROM.write(eepromAddr2, overheatingDifference);
  EEPROM.commit();
  
  Serial.println("Try to set overheating: " + String(overheatingDifference) );
  String m = String("Overheating value set: ") + String(overheatingDifference);
  return m;
}

void HandleNotRstEndpoint(){
  doRestart();
}

String generateHtmlHeader(){
  String h = "<!DOCTYPE html>";
  h += "<html lang=\"en\">";
  h += "\n\t<head>";
  h += "\n\t\t<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  h += "\n\t</head>";
  return h;
}

String generateHtmlBody(){
  String m = "\t<body bgcolor=\"#49B7E8\">";

  m += "\t\t<h4>";
  m += String(SOFTWARE_NAME) + " " + String(VERSION) + " b" + String(BUILDNUM);
  m += "<br>" + String(LOCATION_NAME);
  m += "</h4><br>";

  m += "\n\t<center>\n";

  // HOERZET
  m += "\t\t<h1>";
  m += "H&otilde;&eacute;rzet:\n\r\t\t</h1><h1>";
  m += String(valT);
  m += " &#8451;</h1>\n\r";

  // MERT (ES KORRIGALT) HOMERSEKLET
  m+= "\t\t<h3>(" + String(valC) + " &#8451;)</h3>";

  // BEALLITOTT ERTEK
  m+= "\n\n\t\t<h1>Be&aacute;ll&iacute;tott:</h1>\n";
  m+= "\t\t</h2><h1>" + String(((float)tempSet)/10) + " &#8451;</h1>";
  //m += "<br>";

  // ENERGIA FORRAS VISSZAJELZESE
  String energySource = "";
  if(lastPhaseStatus == String(PHASE_STATUS_CHEAP)){
    energySource = "&darr; &dollar; &darr;";
  }else 
  if(lastPhaseStatus == String(PHASE_STATUS_EXPENSIVE)){
    energySource = "&dollar; &dollar; &dollar;";
  }else{
    energySource = "? &dollar; ?";
  }

  m += "\n\t\t<h2>" + energySource + "</h2>\n\n";


  // FŰTÉS visszajelzése
  if(action == HEATING){
    m += "\t\t<h1>F&Udblac;T&Edot;S</h1>";
  }else{
    m += "\t\t<br>";
  }
  
  // PARATARTALOM
  m += "\n\n\t\t<br><h2>";
  m += "P&aacute;ratartalom:\n\r\t\t</h2><h1>";
  m += String((int)valH);
  m += " %</h1><br>\n";

  //m += generateHtmlSaveValuePart();

  m += "\n\t</center>";

  m += "\n<br>ErrorCount: " + String(errorCount);
  
  #ifndef SKIP_TS_COMMUNICATION
  m += "\n\n<br>Last update status: " + (lastTSUpdateStatus==200?"OK":String(lastTSUpdateStatus));
  #endif
  
  m += "\n<br>Elasped time: " + String(elapsedTime);
  

  m += "<br><a href=\"http://www.idokep.hu/\" target=\"_blank\" title=\"Idojaras\"><img src=\"//www.idokep.hu/terkep/hu_mini/s_anim.gif\"></a>";
  
  m += "\t</body>\r\n";
  m += "</html>";
  
  return m;
}

void HandleRoot(){
  //Serial.println("HandleRoot called...");
  //String message = "<!DOCTYPE html><html lang=\"en\">";
  //message += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head>";
  String message = generateHtmlHeader();
  message += generateHtmlBody();

  server.send(200, "text/html", message );
}

String getCurrentPhaseState(){
     //Declare an object of class HTTPClient
 
    // old
    //http.begin("192.168.1.170:81/data");  //Specify request destination
    // new
    String statusServerPath = "http://" + String(PHASE_CHECKER_IP) + ":81/data";
    http.begin(client, statusServerPath.c_str());  //Specify request destination


    int httpCode = http.GET();                                  //Send the request
    //Serial.print("PhaseStatus - resultCode: " + String(httpCode) + " ps: ");
    
    String payload = "";
    if (httpCode > 0) { //Check the returning code
      payload = http.getString();   //Get the request response payload
      //Serial.print(payload);
      //Serial.println(payload);             //Print the response payload
    }else{
      payload = String(PHASE_STATUS_UNKNOWN);
      Serial.println("httpCode: " + String(httpCode));
    }
    //Serial.println("");
 
    http.end();   //Close connection
    return payload;
}

void HandleData(){
  //Serial.println("HandleData called...");
  String message = "" +  String(valC) + " " +  String(valH);
  server.send(200, "text/html", message );
}

void HandleMultipleData(){
  //Serial.println("HandleData called...");
  lastPhaseStatus = getCurrentPhaseState();
  String message = "" +  String(valC) + "|" +  String(valH) + "|" + lastPhaseStatus;
  server.send(200, "text/html", message );
}


void showPureValues(){
  Serial.println("showPureValues() called...");
  String message = "{\n\r";
  
  message += "\t\"IoT device id\": \"" + String(IOT_DEVICE_ID) + "\",\n\r";
  message += "\t\"location\": \"" + String(LOCATION_NAME) + "\",\n\r";
  message += "\t\"elapsedTime\": \"" + String(elapsedTime) + "\",\n\r";
  message += "\t\"values\": {\n\r";
  message += "\t\t\"tempC\": \"" + String(valC) + "\",\n\r";
  message += "\t\t\"tempF\": \"" + String(valF) + "\",\n\r";
  message += "\t\t\"humidity\": \"" + String(valH) + "\",\n\r";
  message += "\t\t\"heatIndex\": \"" + String(valT) + "\"\n\r";
  message += "\t},\n\r";
  message += "\t\"phaseStatus\": \"" + getCurrentPhaseState() + "\"\n\r";
  message += "}\n\r";
  server.send(200, "text/json", message );
}


// ==========================================================================================================================
//                                                        WiFi handling
// ==========================================================================================================================

int connectToWiFi(String ssid, String pw){
  // Setup WIFI
  WiFi.begin(ssid, password);
  Serial.println("Try to connect (SSID: " + String(ssid) + ")");
  
  // Wait for WIFI connection
  int tryCount = 0;
  while ( (WiFi.status() != WL_CONNECTED) && (tryCount < WIFI_TRY_COUNT)) {
    delay(WIFI_WAIT_IN_MS);
    tryCount++;
    Serial.print(".");
  }

  if(WiFi.status() == WL_CONNECTED){
    return 1;
  }else{
    return 0;
  }
}

int initWiFi(){
  
  if(connectToWiFi(ssid, password)){
    Serial.println("");
    Serial.print("Connected. IP address: ");
    Serial.println(WiFi.localIP());
    String ipMessage = "I" + WiFi.localIP().toString();
    serialOut.println(ipMessage);
    Serial.println("SENT: " + ipMessage );
    // for reconnecting feature
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
  }else{
    Serial.println("");
    Serial.print("ERROR: Unable to connect to ");
    Serial.println(ssid);
    doRestart();
  }
}


// ==========================================================================================================================
//                                                        Init
// ==========================================================================================================================
 
void setup() {

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  elapsedTime = millis();
  
  turnLED(ON);

  errorCount = 0;
  
  Serial.begin(SERIAL_BOUND_RATE);
  Serial.println();
  Serial.println("\n");
  Serial.print(String(TITLE) + " ");
  Serial.println(VERSION);

  serialOut.begin(SOFT_SERIAL_BOUND_RATE);
  Serial.println("SoftSerial initialized");

  // EEPROM
  EEPROM.begin(128);
  // EEPROM read methods can not be used before the first write method..
  tempSet = EEPROM.read(eepromAddr);
  Serial.println("Set temperature: " + String(tempSet));
  overheatingDifference = EEPROM.read(eepromAddr2);
  Serial.println("Overheating difference: " + String(overheatingDifference));

  
  dht.setup(DHT11_PIN, DHTesp::DHT11); //for DHT11 Connect DHT sensor to GPIO 17
  //dht.setup(DHTpin, DHTesp::DHT22); //for DHT22 Connect DHT sensor to GPIO 17

  initWiFi();


  server.on("/", HandleRoot);
  server.on("/data", HandleData);
  server.on("/data2", HandleMultipleData);
  //server.on ("/save", handleSave);
  server.on("/pure", showPureValues);
  server.on("/rst", HandleNotRstEndpoint);
  server.on("/set", actionTempSet);
  server.on("/overheat", actionOverheatSet);
  server.onNotFound( HandleNotFound );
  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() + String("@ port: ") + String(SERVER_PORT) );

  turnLED(OFF);

  Serial.println("PhaseStatus\tStatus\tHumidity (%)\tTemperature (C)\tTempSet\t(F)\tHeatIndex (C)\t(F)");
  lastTemp = -100000;

  delay(10000);
  
}


// ==========================================================================================================================
//                                                        Loop
// ==========================================================================================================================

void wifiConnectionCheck(long now){
  if( (now - lastWiFiCheck) >= DELAY_BETWEEN_WIFI_CONNECTION_STATUS_CHECKS_IN_MS ){
    lastWiFiCheck = now;
    
    switch (WiFi.status()){
      case WL_NO_SSID_AVAIL:
        Serial.println("Configured SSID cannot be reached");
        serialOut.println("IWiFi\nUnavailable");
        turnLED(ON);
        break;
      case WL_CONNECTED:
        turnLED(OFF);
        Serial.println("Connection successfully established");
        break;
      case WL_CONNECT_FAILED:
        turnLED(ON);
        Serial.println("Connection failed");
        serialOut.println("IConnection\nfailed");
        //initWiFi();
        break;
    }
    Serial.printf("Connection status: %d\n", WiFi.status());
    Serial.print("RRSI: ");
    Serial.println(WiFi.RSSI());
  }
}


short decide(){

  int compareValue = tempSet;
  if( lastPhaseStatus == String(PHASE_STATUS_CHEAP) ){
    compareValue += overheatingDifference; // TODO
  }
  
  if( ((int)(valC*10)) < compareValue ){
    action = HEATING;
  }else{
    action = NOTHING;
  }
  return action;
}


void sensorLoop(long now){
  if( (now - lastTemp) > DELAY_BETWEEN_ITERATIONS_IN_MS ){ //Take a measurement at a fixed time (durationTemp = 5000ms, 5s)
    //delay(dht.getMinimumSamplingPeriod());
    //delay(dht.getMinimumSamplingPeriod());
    lastPhaseStatus = getCurrentPhaseState();
    if(lastPhaseStatus == String(PHASE_STATUS_UNKNOWN)){
      // try again one more time
      lastPhaseStatus = getCurrentPhaseState();
    }
    
    lastTemp = now;
    elapsedTime = now;
  
    turnLED(ON);
    float humidity = dht.getHumidity();
    float temperature = dht.getTemperature();
    float ts = ((float)tempSet) / 10;
    turnLED(OFF);

    valC = temperature + TEMPERATURE_CORRECTION;
    valH = humidity;
    valF = dht.toFahrenheit(temperature);
    valT = dht.computeHeatIndex(temperature, humidity, false);
   
    Serial.print("\t" + lastPhaseStatus);
    Serial.print("\t\t");
    Serial.print(dht.getStatusString());
    Serial.print("\t");
    Serial.print(humidity, 1);
    Serial.print("% \t\t");
    Serial.print(temperature, 1);
    Serial.print(" C\t\t");
    Serial.print(ts, 1);
    Serial.print(" C\t");
    Serial.print(valF, 1);
    Serial.print(" F\t\t");
    Serial.print(valT, 1);
    Serial.print("\t\t");
    Serial.println(dht.computeHeatIndex(dht.toFahrenheit(temperature), humidity, true), 1);

    #ifndef SKIP_TS_COMMUNICATION
    String st = dht.getStatusString();
    if(st.length() < 4){
      /*
      if( sendValuesToTSServer(heatindex, humidity, temperature) ){
        // Reset errorCount because we had a normal value reading and a normal online communication after that..
        errorCount = 0;
      }
      /**/
      
      sendValuesToTSServer(valT, humidity, temperature);
    }else{
      //Show temp and/or humidity reading error..
      //ledBlink(5, 1000, 1000);
      
      // Increase error count for restarting after defined max error counts..
      //errorCount++;
    }
    Serial.println("Error count: " + String(errorCount));
    #endif

    decide();

    lastTemp = now;
    int tempInt = valC*10;
    int humidityInt = valH;
    String tempStr = String(tempInt);
    if(tempInt < 100){
      tempStr = "0" + tempStr;
    }
    String setStr = String(tempSet);
    
    if(tempSet < 100){
      setStr = "0" + setStr;
    }
    if(tempSet < 10){
      setStr = "0" + setStr;
    }
    /**/
    
    
    String line = "D" + String(lastPhaseStatus);
    line += "-" + String(humidityInt) + "%";
    line += tempStr + "C";
    line += setStr + "C";
    // "heating?" part
    line += action;

    Serial.println("\"" + line + "\"");
    //serialOut.println("1|25.40|63.00|0");
    serialOut.println(line);
    
  
    if(errorCount >= ERROR_COUNT_BEFORE_RESTART){
      Serial.println("\nDefined error count (" + String(ERROR_COUNT_BEFORE_RESTART) + ") reched!");
      doRestart();
    }
  }
  
}
 
void loop() {
  long now = millis();
  server.handleClient();
  wifiConnectionCheck(now);
  sensorLoop(now);
}
