///Matrix digital clock based on the MDParaola library
// Uses ESP8266 board configured with WiFi Manager

// Header file includes
#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <time.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>

//needed for WiFi Manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"            //https://github.com/tzapu/WiFiManager
WiFiClient client;

const char* Timezone = "GMT0BST,M3.5.0/01,M10.5.0/02";       // UK

//Example time zones
//const char* Timezone = "GMT0BST,M3.5.0/01,M10.5.0/02";     // UK
//const char* Timezone = "MET-2METDST,M3.5.0/01,M10.5.0/02"; // Most of Europe
//const char* Timezone = "CET-1CEST,M3.5.0,M10.5.0/3";       // Central Europe
//const char* Timezone = "EST-2METDST,M3.5.0/01,M10.5.0/02"; // Most of Europe
//const char* Timezone = "EST5EDT,M3.2.0,M11.1.0";           // EST USA
//const char* Timezone = "CST6CDT,M3.2.0,M11.1.0";           // CST USA
//const char* Timezone = "MST7MDT,M4.1.0,M10.5.0";           // MST USA
//const char* Timezone = "NZST-12NZDT,M9.5.0,M4.1.0/3";      // Auckland
//const char* Timezone = "EET-2EEST,M3.5.5/0,M10.5.5/0";     // Asia
//const char* Timezone = "ACST-9:30ACDT,M10.1.0,M4.1.0/3":   // Australia

#define debug true

//Matrix Display connections
#define CLK_PIN   D5
#define DATA_PIN  D7
#define CS_PIN    D8
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
//#define HARDWARE_TYPE MD_MAX72XX::ICSTATION_HW
// Define the number of devices we have in the chain and the hardware interface
#define MAX_DEVICES 4
#define SPEED_TIME  75
#define PAUSE_TIME  0
#define MAX_MESG  50


MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);                   // Hardware SPI
//MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);  // Software SPI

#define BUT_PIN    D6
#define Buz        D3                               //PCB Is designed for D4 so a mod is required on the board as D4 drives the onboard LED

// Global variables
byte alarmH = 7;
byte alarmM = 45;
int  alarmHh;
int  alarmHl;
int  alarmMh;
int  alarmMl;
int h, m, s, dd, dw, mm, yy, ds;
bool alarm   = false;                               // Alarm enabled flag
bool alarmOn = false;                               // Alarm ringing flag
bool butStat = false;                               // Button pressed status
int intensity = 15;                                 // Display brightness
int intensity_old = 16;
long int intenAcc = 0;                              // Intensity accumulator
byte intenCtr = 0;                                  // Intensity counter
byte intenMax = 64;                                 // Max number for Intensity counter

bool webActive = false;                             //web active flag (stops display updating)
uint32_t webTimer = 0;                              //web inactivity timer 

uint32_t lastTime = 0;                              // millis() memory
bool flasher = false;                               // seconds passing flasher

char szTime[MAX_MESG];                              // Time display buffer
char host[25] {"alarm-clock4"};                     //Hostname / default

#include "font.h"                                   //Load updated fonts for the clock
#include "css.h"                                    //Include StyleSheet

WiFiServer server(80);


void configModeCallback (WiFiManager *myWiFiManager) {
  if (debug)Serial.println("Entered config mode");
  if (debug)Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  if (debug)Serial.println(myWiFiManager->getConfigPortalSSID());
}

//Seconds Ticker used to beep the buzzer
Ticker secondTick;


void ICACHE_RAM_ATTR ISRsecTick(void) ;
void getTimeString(char *, bool , bool );
void getTime(void);
void save_settings(void);
void webServer(void);
void sendPage(void);
void sendCSS(void);
void ICACHE_RAM_ATTR buttonInterrupt(void);


void setup(void)
{
  pinMode(Buz, OUTPUT);
  digitalWrite(Buz, LOW);
  if (debug) Serial.begin(115200);
  
  P.begin();
  delay(1000);

  P.setZone(0, 0, MAX_DEVICES - 1 );
  P.displayZoneText(0, szTime, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  //Because I got the PCB design upside down I have to invert the display
  P.setZoneEffect(0, true, PA_FLIP_UD);
  P.setZoneEffect(0, true, PA_FLIP_LR);


  P.addChar(';', space);                            //Substitute narrow space
  P.addChar('0', zero);
  P.addChar('1', one);                              //substitute number 1 with wider spacing
  P.addChar('2', two);
  P.addChar('3', three);
  P.addChar('4', four);
  P.addChar('5', five);
  P.addChar('6', six);
  P.addChar('7', seven);
  P.addChar('8', eight);
  P.addChar('9', nine);

  P.addChar('^', alm);                              //Single pixel dot to indicate alarm on
  P.addChar(':', colon);                            //Narrow Colon
  P.addChar('.', dot);                              //Narrow Dot

  // Initialise or read saved settings from EEPROM
  EEPROM.begin(32);
  if ((EEPROM.read(0)) != 42) {
    if (debug) Serial.println("Saving default settings to EEPROM");
    save_settings();                                // Save default settings to EEPROM
  }
  if (debug) Serial.println("Restoring settings from EEPROM");
  alarmH = EEPROM.read(1);
  alarmM = EEPROM.read(2);
  alarm  = EEPROM.read(3);

  int i = 0;
  while (EEPROM.read(i + 4) > 0) {                  //Restore hostanme from EEPROM
    host[i] = EEPROM.read(i + 4);
    i++;
  }
  host[i] = 0;                                      //Terminate the string with NULL

  sprintf(szTime, "Wifi...");
  P.displayReset();
  P.setIntensity(15);                               // Brightness 0 - 15
  P.displayAnimate();


  wifi_station_set_hostname(host);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(host)) {
    if (debug) Serial.println("failed to connect and hit timeout");
    ESP.restart();
  }

  yield();
  if (debug) {
    Serial.println("WiFi connected");
    // Print Ethernet stats
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("Hostname: %s\n", WiFi.hostname().c_str());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Subnet mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.printf("Gataway IP: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.print("DNS #1, #2 IP: ");
    WiFi.dnsIP().printTo(Serial);
    Serial.print(", ");
    WiFi.dnsIP(1).printTo(Serial);
    Serial.println();
  }
  yield();
  if (WiFi.hostname() != host) ESP.restart();       //reset and try again as hostames dont match

  // Start the WEB server
  server.begin();
  if (debug) Serial.println("Web Server started");

  yield();
  //   Start the mDNS responder using the WiFi Hostname
  if (!MDNS.begin(WiFi.hostname().c_str())) {
    Serial.println("error setting up MDNS responser!");
    ESP.restart();
  }
  //   Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
  if (debug) {
    Serial.println("mDNS responder started");


    Serial.print("Use this URL : ");
    Serial.print("http://");
    Serial.print(WiFi.hostname());
    Serial.println(".local");
    // Print the IP address
    Serial.print("or use this URL : ");
    Serial.print("http://");
    Serial.println(WiFi.localIP());
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1);
  if (debug) Serial.println("NTP Client started");

  alarmHh = alarmH / 10;
  alarmHl = alarmH % 10;
  alarmMh = alarmM / 10;
  alarmMl = alarmM % 10;

  secondTick.attach(1, ISRsecTick);                 // Seconds ticker to operate the alarm buzzer
  pinMode(BUT_PIN, INPUT);                          // Alarm button input active high (pressed)
  attachInterrupt(digitalPinToInterrupt(BUT_PIN), buttonInterrupt, RISING);


}

void loop(void)
{
  // Average LDR ambient light level reading over "intenMax" samples
  // this improves help stop the intensity changing quickly between levels
  if (intenCtr == 0 ) {
    intenCtr = intenMax;
    intensity = round(intenAcc / 64 / intenMax);    //Turn the value to a 4 bit value (0-15)
    intenAcc = 0;
  } else {
    intenAcc = intenAcc + analogRead(A0);           //Add Analogue reading to intensity accumulator
    intenCtr --;
  }



  // Turn on alarm if it's enabled and the time is right
  getTime();
  if (alarm == true && alarmOn == false && alarmH == h && alarmM == m && s < 2) {
    alarmOn = true;
    if (debug) Serial.println("NAlarm On");
  }
    
  if (P.displayAnimate() == true && webActive == false) {

    if (alarmOn == false) {
      // Update display every second
      if (millis() - lastTime >= 1000) {
        lastTime = millis();
        getTimeString(szTime, flasher, alarm);
        flasher = !flasher;
        P.setTextEffect(0, PA_PRINT, PA_NO_EFFECT);
        getTimeString(szTime, flasher, alarm);
        if (intensity != intensity_old) {
          P.setIntensity(intensity);                  // Brightness 0 - 15
          if(debug) Serial.printf("Intensity = %d\n", intensity);
          intensity_old = intensity;
        }
        P.displayReset(0);
      }
    } else {
      P.setTextEffect(0, PA_SCROLL_RIGHT, PA_SCROLL_RIGHT);
      sprintf(szTime, " Alarm");
      P.setIntensity(15);
      P.displayReset(0);
    }
  }

  if(millis() > webTimer +500) webActive = false;      // > 0.5 seconds of web inactivity so cancel 
  webServer();                                         // Check to see if a web client has connected
  MDNS.update();
}

void webServer() {
  // ***** WEB Server *****
  // Check if a client has connected
  client = server.available();
  if (client.available() == 0) { 
    delay(10);
    return;
  }
  webActive = true;                                     //Set web active flag
  webTimer = millis();                                  //Get web active start time
  
  // Read the first line of the request
  String request = client.readStringUntil('\r');
  if (debug) Serial.println(request);
  client.flush();

  // Match the request

  if (request.indexOf("/ethernetcss.css") != -1) {
    client.print(FPSTR(css));                             // Stylesheet requested
    
  } else {    // Process request and send Web page
    if (request.indexOf("?hhup") != -1) {
      alarmHh++;
      if (alarmHh == 10) alarmHh = 0;
    }
    if (request.indexOf("?hlup") != -1) {
      alarmHl++;
      if (alarmHl == 10) alarmHl = 0;
    }
    if (request.indexOf("?mhup") != -1) {
      alarmMh++;
      if (alarmMh == 10) alarmMh = 0;
    }
    if (request.indexOf("?mlup") != -1) {
      alarmMl++;
      if (alarmMl == 10) alarmMl = 0;
    }
    if (request.indexOf("?hhdn") != -1) {
      alarmHh--;
      if (alarmHh == -1) alarmHh = 0;
    }
    if (request.indexOf("?hldn") != -1) {
      alarmHl--;
      if (alarmHl == -1) alarmHl = 0;
    }
    if (request.indexOf("?mhdn") != -1) {
      alarmMh--;
      if (alarmMh == -1) alarmMh = 0;
    }
    if (request.indexOf("?mldn") != -1) {
      alarmMl--;
      if (alarmMl == -1) alarmMl = 0;
    }
    alarmH = alarmHl + (alarmHh * 10);
    alarmM = alarmMl + (alarmMh * 10);
    if (alarmH > 23) alarmH = 23;
    if (alarmM > 59) alarmM = 59;

    if (request.indexOf("?alarmtoggle") != -1) alarm = !alarm;  // Toggle Alarm on and off
    if (request.indexOf("?cancel") != -1) alarmOn = false;      // Cancel Alarm
    if (request.indexOf("?SAVE") != -1) save_settings();        // Save settings to EEPROM
    if (request.indexOf("?host") != -1) {                       // Change Hostname
      if (debug) Serial.print("Hostanme Change received: ");
      String hostStr = request.substring(11, request.length() - 8);
      hostStr.toCharArray(host, hostStr.length());
      if (debug) Serial.println(host);
      wifi_station_set_hostname(host);
      save_settings();                                          // Save settings to EEPROM
      sprintf(szTime, "Reset");                                 ///Display "Reset" while waiting reboot
      ESP.restart();                                            // Re-start ESP to use new hostname
    }
    sendPage();                                                 // Send the web page
  }
}


void sendPage(void) {  // function to send the web page
  client.println("HTTP/1.1 200 OK"); //send new page
  client.println("Content-Type: text/html");
  client.println();
  client.println("<HTML>");
  client.println("<HEAD>");
  client.println("<link rel='stylesheet' type='text/css' href='/ethernetcss.css' />");
  client.println("<TITLE>ESP8266 Matrix Alarm Clock</TITLE>");
  client.println("</HEAD>");
  client.println("<BODY>");
  client.println("<H1>ESP8266 Matrix Alarm Clock</H1>");
  client.print("<hr />");
  client.println("<H2>");

  client.print("Connected to SSID: ");
  client.print(WiFi.SSID());
  client.println("<br />");
  client.print("Use this URL : ");
  client.print("http://");
  client.print(WiFi.hostname());
  client.print(".local");
  client.println("<br />");
  client.print("or use this URL : ");
  client.print("http://");
  client.print(WiFi.localIP());
  client.println("<br />");

  client.printf("Time now: %02d:%02d\n", h, m);
  //client.println("<br />");
  client.println("<br />");
  client.printf("Brightness: %2d\n", intensity);
  client.println("<br />");
  client.println("<br />");

  // Display alarm Status / toggle button
  client.println("<a href=\"?alarmtoggle\"\">");
  if (alarm == true) {
    client.print("Alarm is On");
  } else {
    client.print("Alarm is Off");
  }
  client.println("</a>");

  client.println("<br />");
  client.println("<br />");
  client.println("<br />");

  //Display alarm time and setting buttons.
  alarmHh = alarmH / 10;
  alarmHl = alarmH % 10;
  alarmMh = alarmM / 10;
  alarmMl = alarmM % 10;
  client.println("<a href=\"?hhup\"\">&#8593</a>");
  client.printf("&nbsp&nbsp");
  client.println("<a href=\"?hlup\"\">&#8593</a>");
  client.printf("&nbsp&nbsp&nbsp");
  client.println("<a href=\"?mhup\"\">&#8593</a>");
  client.printf("&nbsp&nbsp&nbsp");
  client.println("<a href=\"?mlup\"\">&#8593</a>");
  client.println("<br />");
  client.println("<br />");
  client.printf("%01d&nbsp&nbsp&nbsp&nbsp&nbsp%01d&nbsp&nbsp:&nbsp&nbsp&nbsp%01d&nbsp&nbsp&nbsp&nbsp&nbsp%01d\n", alarmHh, alarmHl, alarmMh, alarmMl);
  client.println("<br />");
  client.println("<br />");
  client.println("<a href=\"?hhdn\"\">&#8595</a>");
  client.printf("&nbsp&nbsp&nbsp");
  client.println("<a href=\"?hldn\"\">&#8595</a>");
  client.printf("&nbsp&nbsp&nbsp");
  client.println("<a href=\"?mhdn\"\">&#8595</a>");
  client.printf("&nbsp&nbsp&nbsp");
  client.println("<a href=\"?mldn\"\">&#8595</a>");

  client.print("<br />");
  client.print("<br />");
  client.print("<br />");

  //Update Hostname form
  client.println("<form name=\"myform\" action=\"/\" method=\"GET\">");
  client.print("<input type=\"text\" style=\"font-size: 32pt\" name=\"host\" size=\"25\" value=");
  client.print(WiFi.hostname());
  client.println(">");
  client.println("<br><br><input type=\"submit\" style=\"font-size: 100%; color: white; background-color: #293F5E\" value=\"Update Hostname\"><br>");
  client.println("</form>");

  // Save and Refresh Buttons
  client.println("<a href=\"?SAVE\"\">Save Settings</a>");
  client.print("<br />");
  client.print("<br />");
  client.print("<br />");
  client.println("<a href=\"/\">Update Page</a>");
  client.print("<br />");
  client.println("</H2>");
  client.println("</BODY>");
  client.println("</HTML>");
}



// Function to save settings to EEPROM
void save_settings() {
  Serial.println("Saving to EEPROM");
  EEPROM.write(0, 42);                                    // EEPROM contents valid flag 42 is valid
  EEPROM.write(1, alarmH);                                // Alarm Hour
  EEPROM.write(2, alarmM);                                // Alarm Minute
  EEPROM.write(3, alarm);                                 // Alarm on / off flag
  if (debug) Serial.println("Writing Hostname:");
  int i = 0;
  while (host[i] > 0 && i < 25) {
    EEPROM.write(i + 4, host[i]);                         // Up to 25 bytes of hostname
    i++;
  }
  EEPROM.write(i + 4, 0);                                 // Terminate with NULL
  EEPROM.commit();
  if (debug) Serial.println("Settings Saved");
}

// Function to read the clock time
void getTimeString(char *psz, bool f = true, bool al = false)
{
  getTime();
  sprintf(psz, "%c%02d%c%02d", (al ? '^' : ';'), h, (f ? ':' : ';'), m);

}

// Function to read the clock time
void getTime(void)
{
  time_t now;
  time(&now);
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  h  = timeinfo->tm_hour;
  m  = timeinfo->tm_min;
  s  = timeinfo->tm_sec;
  dd = timeinfo->tm_mday;
  dw = timeinfo->tm_wday;
  mm = timeinfo->tm_mon;
  yy = 1900 + timeinfo->tm_year;
  ds = timeinfo->tm_isdst;
}

// 1 second ticker routine to handle buzzer beeps on and off every second
void ICACHE_RAM_ATTR ISRsecTick() {
  if (alarmOn == true) {
    digitalWrite(Buz, !digitalRead(Buz));
  } else {
    digitalWrite(Buz, LOW);
  }
}

//Interrupt routine to handle touch button presses
void ICACHE_RAM_ATTR buttonInterrupt() {
  if (debug) Serial.println("Interrupt Detected");

  if (alarmOn == true) {
    alarmOn = false;
    if (debug) Serial.println("Alarm cancelled");
  } else {
    alarm = !alarm;                                     // Toggle Alarm on/off
    save_settings();                                    // Save new Alarm setting in EEPROM  
  }
  lastTime = -1000;                                     // Force display to update immediately
  
}
