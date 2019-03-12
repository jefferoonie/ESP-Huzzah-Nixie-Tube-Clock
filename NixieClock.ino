// This is hard coded for daylight savings US Central time zone.
// I plan on making that controllable.
// Time Server is also hard coded.

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <Wire.h>

const char* SSID_NAME001 = "someSSID";
const char* SSID_PASS001 = "somePAssword";
const char* SSID_NAME002 = "";
const char* SSID_PASS002 = "";
const char* SSID_NAME003 = "";
const char* SSID_PASS003 = "";

const int           NTP_PACKET_SIZE = 48;               // NTP time stamp is in the first 48 bytes of the message
const char*         NTP_SERVER_NAME = "time.nist.gov";
const unsigned long NTP_INTERVAL = 3600000;             // Request NTP time every hour
const unsigned long NTP_WAIT = 10800000;                // How long to let clock go without talking to NTP server

//Toggle Timezones
const unsigned long TIMEZONE_OFFSET = 18000; 
//const unsigned long TIMEZONE_OFFSET = 21600;

ESP8266WiFiMulti  wifiMulti;      
WiFiUDP           UDP;                     
IPAddress         timeServerIP;

byte ntpBuffer[NTP_PACKET_SIZE];

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
}

unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
unsigned long prevActualTime = 0;

void loop()
{
  unsigned long currentMillis = millis();

  // Send an NTP request, as long as an hour has past, or on power up.
  if (currentMillis - prevNTP > NTP_INTERVAL) 
  { 
    prevNTP = currentMillis;
    sendNTPPacket(timeServerIP);
  }

  // Check if an NTP response has arrived and get the (UNIX) time
  uint32_t time = getTime();                  
  if (time)                                   // If a new timestamp has been received
  {                                  
    lastNTPResponse = currentMillis;
  } 
  else if ((currentMillis - lastNTPResponse) > NTP_WAIT) 
  {
    //Reset ESP if NTP_WAIT time has passed.
    ESP.reset();
  }

  uint32_t actualTime = time + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && time != 0) 
  { 
    prevActualTime = actualTime;
   
    Wire.beginTransmission(0x20); //starts talking to slave device
    Wire.write(0x12); //selects the GPIOA pins
    Wire.write(dec2bcd(getHours(actualTime))); // turns on pins 0 and 1 of GPIOA
    Wire.endTransmission(); //ends communication with the device

    Wire.beginTransmission(0x20); //starts talking to slave device
    Wire.write(0x13); //selects the GPIOB pins
    Wire.write(dec2bcd(getMinutes(actualTime))); //turns on pin 0 of GPIOA
    Wire.endTransmission();//ends communication with1the device
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

uint32_t getTime() {
  // If there's no response (yet)
  if (UDP.parsePacket() == 0) 
  { 
    return 0;
  }
  
  UDP.read(ntpBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t ntpTime = (ntpBuffer[40] << 24) | (ntpBuffer[41] << 16) | (ntpBuffer[42] << 8) | ntpBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t unixTime = ntpTime - seventyYears;
  unixTime = unixTime - TIMEZONE_OFFSET;
  return unixTime;
}

void sendNTPPacket(IPAddress& address) 
{
  memset(ntpBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  ntpBuffer[0] = 0b11100011;   // LI, Version, Mode
  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(ntpBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getSeconds(uint32_t unixTime) 
{
  return unixTime % 60;
}

inline int getMinutes(uint32_t unixTime) 
{
  return unixTime / 60 % 60;
}

inline int getHours(uint32_t unixTime) 
{ 
  int hr12;
  int hr24 = unixTime / 3600 % 24;

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
