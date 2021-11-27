#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <Wire.h>

const char* SSID_NAME001 = "SomeSSID";
const char* SSID_PASS001 = "SomePWD";
const char* SSID_NAME002 = "";
const char* SSID_PASS002 = "";
const char* SSID_NAME003 = "";
const char* SSID_PASS003 = "";

const char* MDNS_NAME = "nixie";

const char*         NTP_SERVER_NAME = "time.nist.gov";  // NTP Server Address
const int           NTP_PACKET_SIZE = 48;               // NTP time stamp is in the first 48 bytes of the message
const unsigned long NTP_INTERVAL    = 3600000;          // Request NTP time every hour
const unsigned long NTP_WAIT = 10800000;                // How long to let clock go without talking to NTP server

const int           EEPROM_SIZE = 4;

ESP8266WiFiMulti  wifiMulti;      
WiFiUDP           UDP;                     
IPAddress         timeServerIP;

byte ntpBuffer[NTP_PACKET_SIZE];

ESP8266WebServer server(80);

void setup() 
{
  delay(10);

  startWiFi();         
  startUDP();

  if(!WiFi.hostByName(NTP_SERVER_NAME, timeServerIP)) 
  {
    ESP.reset();
  }

  sendNTPPacket(timeServerIP);

  // Set up MCP23017
  Wire.begin(); //creates a Wire object

  // set MCP23017 I/O pins to outputs
  Wire.beginTransmission(0x20); //begins talking to the slave device
  Wire.write(0x00); //selects the IODIRA register
  Wire.write(0x00); //this sets all port A pins to outputs
  Wire.endTransmission(); //stops talking to device
  Wire.beginTransmission(0x20);//begins talking again to slave device
  Wire.write(0x01); //selects the IODIRB register
  Wire.write(0x00); // sets all port B pins to outputs
  Wire.endTransmission(); //ends communication with slave device

  //Init EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  //SetUp MDNS
  MDNS.begin(MDNS_NAME);

  //Seup HTTP Server
  //URLs to handle.
  server.on("/", handleRoot);
  server.on("/tz", tzCode); 
  server.onNotFound(handleNotFound);

  server.begin();
}

unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
unsigned long prevActualTime = 0;
uint32_t unixTimeLocal = 0;

void loop()
{
  MDNS.update();
  server.handleClient();

  unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > NTP_INTERVAL) {
    prevNTP = currentMillis;
    sendNTPPacket(timeServerIP);
  }

  uint32_t time = getTime();
  if (time) {
    unixTimeLocal = time;
    lastNTPResponse = currentMillis;                                 
  } 
  else if ((currentMillis - lastNTPResponse) > NTP_WAIT) 
  {
    ESP.reset();
  }

  uint32_t actualTime = unixTimeLocal + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && unixTimeLocal != 0) 
  { 
    prevActualTime = actualTime;
   
    Wire.beginTransmission(0x20); //starts talking to slave device
    Wire.write(0x12); //selects the GPIOA pins
    Wire.write(dec2bcd(getHours(actualTime))); // turns on pins 0 and 1 of GPIOA
    Wire.endTransmission(); //ends communication with the device

    Wire.beginTransmission(0x20); //starts talking to slave device
    Wire.write(0x13); //selects the GPIOB pins
    Wire.write(dec2bcd(getMinutes(actualTime))); //turns on pin 0 of GPIOA
    Wire.endTransmission();//ends communication with the device
  }  
}

// Try to connect to some given access points. Then wait for a connection
void startWiFi() 
{ 
  wifiMulti.addAP(SSID_NAME001, SSID_PASS001);
  wifiMulti.addAP(SSID_NAME002, SSID_PASS002);
  wifiMulti.addAP(SSID_NAME003, SSID_PASS003);
  
  // Wait for the Wi-Fi to connect
  while (wifiMulti.run() != WL_CONNECTED) 
  {
    delay(250);
  }
}

void startUDP() 
{
   // Start listening for UDP messages on port 123
  UDP.begin(123);
}

uint32_t getTime()
{
  //If there's no response (yet)
  if (UDP.parsePacket() == 0) 
  { 
    return 0;
  }

  //GetTimeZoneOffSet
  long tzCode;
  int address = 0;
  EEPROM.get(address, tzCode);

  //read the packet into the buffer
  UDP.read(ntpBuffer, NTP_PACKET_SIZE);
  //Combine the 4 timestamp bytes into one 32-bit number
  uint32_t ntpTime = (ntpBuffer[40] << 24) | (ntpBuffer[41] << 16) | (ntpBuffer[42] << 8) | ntpBuffer[43];
  
  //Convert NTP time to a UNIX timestamp:
  //Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  uint32_t uxTime = ntpTime - seventyYears;
  
  uxTime = uxTime + tzCode;
  return uxTime;
}

void sendNTPPacket(IPAddress& address) 
{
  memset(ntpBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  //Initialize values needed to form NTP request
  ntpBuffer[0] = 0b11100011;   // LI, Version, Mode
  //send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(ntpBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getMinutes(uint32_t uxTime) 
{
  return uxTime / 60 % 60;
}

inline int getHours(uint32_t uxTime) 
{ 
  int hr12;
  int hr24 = uxTime / 3600 % 24;

  if (hr24==0 or hr24==12) hr12=12;
  else hr12=hr24%12;
  return hr12;
}

//Decimal to BCD.
byte dec2bcd(byte dec)
{
  byte result = 0;
  
  result |= (dec / 10) << 4;
  result |= (dec % 10) << 0;
  
  return result;
}

void handleRoot() 
{  
  int address = 0;
  long tzCode;
  EEPROM.get(address, tzCode);
  
  String message01 = "<!DOCTYPE html><html><body>"
                     "<h1>Nixie Clock</h1>"
                     "<br><b>Current Time Zone Code: </b>";
  
  String message02 = "<br>"
                     "<br><a href=\"tz?Code=-18000\">-18000 - Eastern</a>"
                     "<br><a href=\"tz?Code=-14400\">-14400 - Eastern Daylight Savings</a>"                    
                     "<br><a href=\"tz?Code=-21600\">-21600 - Central</a>"
                     "<br><a href=\"tz?Code=-18000\">-18000 - Central Daylight Savings</a>"
                     "<br><a href=\"tz?Code=-25200\">-25200 - Mountain</a>"
                     "<br><a href=\"tz?Code=-21600\">-21600 - Mountain Daylight Savings</a>"
                     "<br><a href=\"tz?Code=-28800\">-28800 - Pacific</a>"
                     "<br><a href=\"tz?Code=-25200\">-25200 - Pacific Daylight Savings</a>"  
                     "<br><a href=\"tz?Code=-32400\">-32400 - Alaska</a>"
                     "<br><a href=\"tz?Code=-28800\">-28800 - Alaska Daylight Savings</a>"
                     "<br><a href=\"tz?Code=-28800\">-36000 - Hawaii</a>"
                     "</body></html>";
                      
  String message = message01 + tzCode + message02;
  
  server.send(200, "text/html", message);
}

void handleNotFound()
{
  String message = "File Not Found ";
  message += "\nURI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void tzCode() 
{ 
  String message = "";

  if (server.arg("Code")== ""){     
    message = "Code Argument not found";
  }else{
    long timeZoneCode = server.arg("Code").toDouble();
    int address = 0;
    EEPROM.put(address, timeZoneCode);
    EEPROM.commit();
    
    message = "Time zone code set to: ";
    message += timeZoneCode;
  }
 
  server.send(200, "text/plain", message);
  delay(5);
  ESP.reset();
}
