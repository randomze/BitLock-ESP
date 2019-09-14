#include <map>
#include <queue>

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
const uint16_t UDPSlavePort = 2016;
const uint16_t TCPSlavePort = 2019;

bool waitingForRegister;

ESP8266WebServer serverGateway(80);
ESP8266WebServer *serverMaster;
WiFiUDP udp;
HTTPClient client;

String master_unique;
String ssid;
String password;

IPAddress broadcastAddress;

int gatewayOrMaster;

int EEPROMaddress;

std::map<String, WiFiClient> slaveMap;

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

    setupMaster();
  }
}

//CLIENT CODE

void checkForMessage() {
  const char* host = "https://bitlock-api.herokuapp.com/devices/waiting/";

  String thumbprint = "08:3B:71:72:02:43:6E:CA:ED:42:86:93:BA:7E:DF:81:C4:BC:62:30";

  if (client.begin(String(host) + master_unique, thumbprint)) {
    int statusCode = client.GET();
    String response;
    if (statusCode > 0) {
      response = client.getString();
    }
    if (response.startsWith("OPEN")) {
      String slaveId = response.substring(6);
      slaveId.trim();
      if(slaveMap.find(slaveId) != slaveMap.end()) {
        WiFiClient client  = slaveMap[slaveId];
        if(client.connected()) {
          Serial.println("Connected to " + slaveId);
          client.print("OPEN\n");
          client.flush();
        } else {
          Serial.println("Couldnt connect to " + slaveId);
        }
      }
    }
  }
}

void setup(void)
{
  Serial.begin(9600);
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_AP_STA);
  char buffer[65];

  master_unique = "";
  int i;
  EEPROMaddress = 0;
  waitingForRegister = 0;

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

    gatewayOrMaster = 1;
    setupMaster();
    if (gatewayOrMaster == 0) {
      setupConnectGateway();
    }
  } else {
    gatewayOrMaster = 0;
    setupConnectGateway();
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
  if (gatewayOrMaster == 1) {
    delay(500);
    findSlavesOnNetwork();
    serverMaster->handleClient();
    receivePacket();
    checkForMessage();
  } else {
    serverGateway.handleClient();
  }
}


void findSlavesOnNetwork()
{
  char message[] = "Anyone there?";
  sendPacket((uint8_t *) message, strlen(message) + 1);

  int i = 0;
  while(!udp.parsePacket()) {
    i++;
    delay(400);
    if(i > 5)
      break;
  }

  String packet = receivePacket();

  Serial.println(packet);

  if(packet.equals("Yes")) {
    Serial.println("FOUND SLAVE");
    WiFiClient client;
    if(client.connect(udp.remoteIP(), TCPSlavePort)) {
      client.print("REG_WAIT?\n");
      client.flush();
      Serial.println("SUCESSFULLY CONNECTED TO SLAVE OVER TCP");
      if(client.available()) {
        String response = client.readStringUntil('\n');
        Serial.println("response: " + response);
        if(response.equals("YES")) {
          Serial.println("response==YES");
          slaveMap[""] = client;
          waitingForRegister = true;
        } else {
          Serial.println("response!=YES");
          slaveMap[response] = client;
        }
      }
    } else {
      Serial.println("tcp failed");
    }
  }
}

//
//
//   UDP helper functions
//
//

bool sendPacket(const uint8_t* buf, uint8_t bufSize) {
  udp.beginPacket(broadcastAddress, TCPSlavePort);
  udp.write(buf, bufSize);
  return (udp.endPacket() == 1);
}

String receivePacket() {
  char buffer[255];
  buffer[0] = 0;

  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(buffer, 255);
    if (len > 0) {
      buffer[len] = 0;
    }
  }

  if (strcmp(buffer, "Finding Bitlock") == 0) {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write("You found me, lying on the floor");
    udp.endPacket();
    return "";
  } else {
    return String(buffer);
  }
}

//
//
//   serverMaster page handling functions
//
//

void handleClientRoot() {
  if (serverMaster->method() != HTTP_POST) {
    if (serverMaster->method() == HTTP_GET && master_unique != "") {
      serverMaster->send(200, "text/plain", master_unique);
    } else {
      serverMaster->send(200, "text/plain", "ITS A ME, BITLOCK!");
    }
  }
  else if (master_unique = "")
  {
    master_unique = serverMaster->arg("plain");

    EEPROM.begin(256);

    unsigned int i = 0;
    master_unique = String("id=") + master_unique;
    for (; i < master_unique.length(); i++) {
      EEPROM.write(EEPROMaddress + i, master_unique[i]);
      delay(200);
    }

    EEPROM.write(EEPROMaddress + i, '\0');
    delay(200);

    EEPROM.commit();

    master_unique = master_unique.substring(4);

    serverMaster->send(200, "text/plain", "OK!");
  } else {
    serverMaster->send(200, "text/plain", "Already registered, please stop");
  }
}

void handleClientRegisterDevice() {
  if (serverMaster->method() == HTTP_GET) {
    serverMaster->send(200, "text/plain", waitingForRegister ? "Device waiting to be registered" : "No devices available for registering");

  } else if (serverMaster->method() == HTTP_POST) {
    String device_id = serverMaster->arg("plain");

    if (waitingForRegister) {
      WiFiClient client = slaveMap[""];
      if(client.connected()) {
        Serial.println("Connected to slave to be registered");
        client.print("REGISTER AS ");
        client.print(device_id);
        client.print("\n");
        client.flush();

        if(client.available()) {
          String response = client.readStringUntil('\n');
          Serial.println("response: " + response);
          if(response.equals("DONE")) {
            Serial.println("response==DONE");
            slaveMap[device_id] = client;
            slaveMap.erase("");
            waitingForRegister = false;
            serverMaster->send(200, "text/plain", "Device sucessfully registered");
          } else {
            Serial.println("response!=DONE");
            serverMaster->send(200, "text/plain", "Unable to register device");
          }
        }
      } else {
        Serial.println("Fail to connect to slave to be registered");
      }

    } else {
      Serial.println("No devices available");
      serverMaster->send(200, "text/plain", "No devices available for registering");
    }
  } else if (serverMaster->method() == HTTP_DELETE) {
    String device_id = serverMaster->arg("plain");

    if(slaveMap.find(device_id) != slaveMap.end()) {
      WiFiClient client = slaveMap[device_id];
      if(client.connected()) {
        client.print("DELETE\n");
        client.flush();
        client.stop();

        slaveMap.erase(device_id);
      }
    } else {
      serverMaster->send(200, "text/plain", "No such device");
    }
  } else {
    serverMaster->send(405, "text/plain", "Method not allowed");
  }
}

//
//
//   Setup functions for the different running modes
//
//

void setupMaster() {
  Serial.println("1");
  WiFi.begin(ssid, password);

  Serial.println("2");
  int result;
  Serial.println("3");
  int tries = 1;
  Serial.println("4");
  while ((result = WiFi.status()) != WL_CONNECTED && tries < 20) {
    Serial.println("5");
    tries++;
    Serial.println("6");
    delay(500);
    Serial.println("7");
  }
  Serial.println("8");
  if (result == WL_CONNECTED) {
    Serial.println("9");
    Serial.println("connected sucessfully to wifi");
    if (gatewayOrMaster == 0) {
      serverGateway.send(200, "text/plain", "Sucessfully connected");
      serverGateway.stop();
      WiFi.softAPdisconnect();
    }

    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP("BitLockMesh", "bitlockmesh", 1, 1);

    Serial.println("does this happen");

    serverMaster = new ESP8266WebServer(WiFi.localIP(), 80);
    if (EEPROMaddress == 0) {
      unsigned int i;
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

    serverMaster->on("/", handleClientRoot);
    serverMaster->on("/devices/", handleClientRegisterDevice);
    serverMaster->on("/reset/", handleReset);

    serverMaster->begin();

    //NEVER FORGET, EURO 2004
    udp.begin(2004);
    gatewayOrMaster = 1;
  } else {
    serverGateway.send(500, "text/plain", "Failed to connect");
    gatewayOrMaster = 0;
  }
}

void setupConnectGateway(void)
{
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("Bit_Lock");

  serverGateway.on("/", handleServerRoot);

  serverGateway.begin();
}