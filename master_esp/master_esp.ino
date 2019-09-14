#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define IP(a,b,c,d) (uint32_t)(a | (b << 8) | (c << 16) | (d << 24))

const uint32_t local_IP = IP(192, 168, 20, 1);
const uint32_t gateway = local_IP;
const uint32_t subnet = IP(255, 255, 255, 0);

const uint16_t masterPort = 2004;
const uint16_t slavePort = 2016;

ESP8266WebServer serverGateway(80);
ESP8266WebServer *serverClient;
WiFiUDP udp;
HTTPClient client;

String master_unique;
String ssid;
String password;

IPAddress broadcastAddress;

//clientserver = 0 means its in soft ap clientserver in order to find a network to connect to. clientserver = 1 means its in client clientserver and connected to a wifi network
int serverclient;

int EEPROMaddress;

char packetBuffer[255]; //buffer to hold incoming packet
char ReplyBuffer[] = "acknowledged";       // a string to send back

//SERVER CODE

const char INDEX_HTML_PRELIST[] =
  "<!DOCTYPE HTML>"
  "<html>"
  "<head>"
  "<meta name = \"viewport\" content = \"width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0\">"
  "<title>Bit Lock</title>"
  "<style>"
  "\"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\""
  "</style>"
  "</head>"
  "<body>"
  "<h1>Bit Lock</h1>"
  "<FORM action=\"/\" method=\"post\">"
  "<P>"
  "<SELECT name=\"network\">";

const char INDEX_HTML_POSTLIST[] =
  "</select> <br>"
  "<INPUT type=\"text\" name=\"password\">"
  "<br>"
  "<INPUT type=\"submit\" value=\"Connect\">"
  "</P>"
  "</FORM>"
  "</body>"
  "</html>";

// GPIO#0 is for Adafruit ESP8266 HUZZAH board. Your board LED might be on 13.

void handleServerRoot()
{
  if (serverGateway.hasArg("password")) {
    handleSubmit();
  }
  else {
    int numNetworks = WiFi.scanNetworks();

    String INDEX_HTML = INDEX_HTML_PRELIST;
    for (int i = 0; i < numNetworks; i++)
    {
      INDEX_HTML += "<option value=\"";
      INDEX_HTML += WiFi.SSID(i);
      INDEX_HTML += "\">";
      INDEX_HTML += WiFi.SSID(i);
      INDEX_HTML += "</option>\n";
    }
    INDEX_HTML += INDEX_HTML_POSTLIST;

    serverGateway.send(200, "text/html", INDEX_HTML);
  }
}

void handleSubmit()
{
  if (!serverGateway.hasArg("network") || !serverGateway.hasArg("password")) {
    serverGateway.send(404, "text/plain", "Missing arguments: network or password");
  } else {
    ssid = serverGateway.arg("network");
    password = serverGateway.arg("password");

    setupClient();
  }
}

void setupSoftAP(void)
{
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("Bit_Lock");

  serverGateway.on("/", handleServerRoot);

  serverGateway.begin();
}

//CLIENT CODE

void setupClient() {
  WiFi.begin(ssid, password);

  int result;
  int tries = 1;
  while((result = WiFi.status()) != WL_CONNECTED && tries < 20) {
    tries++;
    delay(500);
  }
  if (result == WL_CONNECTED) {
    if (serverclient == 0) {
      serverGateway.send(200, "text/plain", "Sucessfully connected");
      serverGateway.stop();
      WiFi.softAPdisconnect();
    }

    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP("BitLockMesh", "bitlockmesh", 1, 1);

    Serial.println("does this happen");

    serverClient = new ESP8266WebServer(WiFi.localIP(), 80);
    if (EEPROMaddress == 0) {
      int i;
      String temp = "wl=";
      EEPROM.begin(256);
      for (i = 0; i < 3; i++) {
        EEPROM.write(EEPROMaddress + i, temp[i]);
        delay(200);
      }
      EEPROMaddress += i;

      for (i = 0; i < ssid.length(); i++) {
        EEPROM.write(EEPROMaddress + i, ssid[i]);
        delay(200);
      }
      EEPROM.write(EEPROMaddress + i, '\0');

      EEPROMaddress += i + 1;
      for (i = 0; i < password.length(); i++) {
        EEPROM.write(EEPROMaddress + i, password[i]);
        delay(200);
      }
      EEPROM.write(EEPROMaddress + i, '\0');
      delay(200);

      EEPROM.commit();

      EEPROMaddress += i + 1;
    }

    broadcastAddress = (uint32_t)WiFi.softAPIP() | ~subnet;

    serverClient->on("/", handleClientRoot);
    serverClient->on("/devices/", handleClientRegisterDevice);
    serverClient->on("/reset", handleReset);

    serverClient->begin();

    //NEVER FORGET, EURO 2004
    udp.begin(2004);
    serverclient = 1;
  } else {
    serverGateway.send(500, "text/plain", "Failed to connect");
    serverclient = 0;
  }
}

void handleClientRoot() {
  if (serverClient->method() != HTTP_POST) {
    if (serverClient->method() == HTTP_GET && master_unique != "") {
      serverClient->send(200, "text/plain", master_unique);
    } else {
      serverClient->send(200, "text/plain", "ITS A ME, BITLOCK!");
    }
  }
  else if (master_unique = "")
  {
    master_unique = serverClient->arg("plain");

    EEPROM.begin(256);

    int i = 0;
    master_unique = String("id=") + master_unique;
    for (i; i < master_unique.length(); i++) {
      EEPROM.write(EEPROMaddress + i, master_unique[i]);
      delay(200);
    }

    EEPROM.write(EEPROMaddress + i, '\0');
    delay(200);

    EEPROM.commit();

    master_unique = master_unique.substring(4);

    serverClient->send(200, "text/plain", "OK!");
  } else {
    serverClient->send(200, "text/plain", "Already registered, please stop");
  }
}

void handleClientRegisterDevice() {
  char response[255];
  char message[255] = "\0";

  if (serverClient->method() == HTTP_GET) {
    while (!sendPacket((uint8_t *)strcat(message, "REG_WAIT?"), strlen(message) + 1)) Serial.println("trying to send packet");

    delay(1000);
    int packetSize = udp.parsePacket();
    if (packetSize) {
      int len = udp.read(response, 255);
      if (len > 0) {
        response[len] = 0;
      }
    }

    Serial.println(response);

    if (strcmp(response, "NO") == 0) {
      serverClient->send(200, "text/plain", "No device to register");
    } else if (strcmp(response, "YES") == 0) {
      serverClient->send(200, "text/plain", "Device waiting to be registered");
    } else {
      serverClient->send(200, "text/plain", "Unable to get a proper response");
    }

  } else if (serverClient->method() == HTTP_POST) {
    String device_id = serverClient->arg("plain");

    while (!sendPacket((uint8_t* )strcat(strcat(message, "REGISTER AS "), device_id.c_str()), strlen(message))) Serial.println("trying to send packet");

    int packetSize = udp.parsePacket();
    if (packetSize) {
      int len = udp.read(response, 255);
      if (len > 0) {
        response[len] = 0;
      }
    }
    if (strcmp(response, "DONE") == 0) {
      serverClient->send(200, "text/plain", "DONE");
    } else {
      serverClient->send(200, "text/plain", "Unable to register");
    }
  } else if(serverClient->method() == HTTP_DELETE) {
    String device_id = serverClient->arg("plain");

    while (!sendPacket((uint8_t* )strcat(strcat(message, "DELETE "), device_id.c_str()), strlen(message))) Serial.println("trying to send packet");    
  } else {
    serverClient->send(405, "text/plain", "Method not allowed");
  }
}

void checkForMessage() {
  const char* host = "https://bitlock-api.herokuapp.com/devices/waiting/";

  String thumbprint = "08:3B:71:72:02:43:6E:CA:ED:42:86:93:BA:7E:DF:81:C4:BC:62:30";
  
  if (client.begin(String(host) + master_unique, thumbprint)) {
    int statusCode = client.GET();
    String response;
    if (statusCode > 0) {
      response = client.getString();
    }
    if(response.startsWith("OPEN")) {
      while (!sendPacket((uint8_t *)response.c_str(), response.length() + 1)) Serial.println("trying to send packet");  
    }
  }
}

bool sendPacket(const uint8_t* buf, uint8_t bufSize) {
  udp.beginPacket(broadcastAddress, slavePort);
  udp.write(buf, bufSize);
  return (udp.endPacket() == 1);
}

void setup(void)
{
  Serial.begin(9600);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoConnect(false);
  char buffer[65];

  master_unique = "";
  int i;
  EEPROMaddress = 0;

  EEPROM.begin(256);

  for (i = 0; i < 3; i++) {
    buffer[i] = EEPROM.read(i);
  }

  if (buffer[0] == 'w' && buffer[1] == 'l' && buffer[2] == '=') {

    EEPROMaddress += i;
    for (i = 0; (buffer[i] = EEPROM.read(EEPROMaddress + i)) != '\0'; i++) {
      ssid += buffer[i];
    }

    EEPROMaddress += i + 1;
    for (i = 0; (buffer[i] = EEPROM.read(EEPROMaddress + i)) != '\0'; i++) {
      password += buffer[i];
    }

    EEPROMaddress += i + 1;
    for (i = 0; i < 3; i++) {
      buffer[i] = EEPROM.read(EEPROMaddress + i);
    }

    if (buffer[0] == 'i' && buffer[1] == 'd' && buffer[2] == '=') {

      EEPROMaddress += i;
      for (i = 0; (buffer[i] = EEPROM.read(EEPROMaddress + i)) != '\0'; i++) {
        master_unique += buffer[i];
      }
      EEPROMaddress += i + 1;
    }

    serverclient = 1;
    setupClient();
    if(serverclient == 0) {
      setupSoftAP();
    }
  } else {
    serverclient = 0;
    setupSoftAP();
  }

}

void handleReset() {
  EEPROM.begin(256);
  EEPROM.write(0, '\0');
  EEPROM.commit();
  ESP.restart();
}

void loop(void)
{
  if (serverclient == 1) {
    delay(1000);
    serverClient->handleClient();
    int packetSize = udp.parsePacket();
    if (packetSize) {
      IPAddress remoteIp = udp.remoteIP();

      int len = udp.read(packetBuffer, 255);
      if (len > 0) {
        packetBuffer[len] = 0;
      }
      
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write(ReplyBuffer);
      udp.endPacket();
    }
    checkForMessage();
  } else {
    serverGateway.handleClient();
  }
}
