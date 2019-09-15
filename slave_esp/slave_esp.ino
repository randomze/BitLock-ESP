#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

const uint16_t UDPPort = 2016;

WiFiUDP udp;

char packetBuffer[255];
String id;
bool deleteMe;

void setup() {
  Serial.begin(9600);

  id = "";
  deleteMe = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin("BitLockMesh", "bitlockmesh");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println(".");
    delay(1000);
  }

  readIDFromEEPROM();

  udp.begin(2016);
}

void loop() {
  
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // read the packet into packetBufffer
    int len = udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }

    String request = packetBuffer;
    Serial.println(request);
    String reply = "";
    if (request.equals("REG_WAIT?")) {
      if(id.length() == 0) {
        reply = "YES";  
      } else {
        reply = "NO";
      }
    } else if(request.startsWith("REGISTER AS ")) {
      String newId = request.substring(13);
      if(id.length() == 0) {
        id = newId;
        writeIDToEEPROM();
        reply = "DONE";
      } else {
        reply = "CANT";
      }
    } else if(request.startsWith("OPEN ")) {
      String targetId = request.substring(6);
      if(targetId.equals(id)) {
        Serial.print("open\n");
      }
    }

    // send a reply, to the IP and port that sent us the packet we received
    if (reply.length() > 0) {
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      char buffer[255];
      reply.toCharArray(buffer, 255);
      udp.write(buffer);
      udp.endPacket();
    }
  }

  if (deleteMe) {
    EEPROM.begin(256);
    EEPROM.write(0, '\0');
    EEPROM.commit();
    ESP.restart();
  }
}

void writeIDToEEPROM() {
  EEPROM.begin(256);

  String temp = "id+" + id;
  for(unsigned int i = 0; i < temp.length(); i++) {
    EEPROM.write(i, temp[i]);
    delay(100);
  }

  EEPROM.write(temp.length(), '\0');
  delay(100);

  EEPROM.commit();
}

void readIDFromEEPROM() {
  String temp;

  EEPROM.begin(256);
  for(int i = 0; i < 3; i++) {
    temp += EEPROM.read(i);
    delay(200);
  }

  if(temp.startsWith("id=")) {
    char c;
    for(int i = 3; (c = EEPROM.read(i)) != '\0'; i++) {
      id += c;
      delay(200);
    }
  } else {
    id = "";
  }
}