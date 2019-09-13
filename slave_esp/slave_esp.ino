#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

WiFiUDP udp;

char packetBuffer[255];
String id;
bool deleteMe;

void setup() {
  id = "";
  deleteMe = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin("BitLockMesh", "bitlockmesh");

  pinMode(2, INPUT);

  EEPROM.begin(256);

  String temp;
  for (int i = 0; i < 3; i++) {
    temp += EEPROM.read(i);
  }

  if (temp.equals("id=")) {
    char holder;
    for (int i = 3; (holder = EEPROM.read(i)) != '\0'; i++) {
      id += holder;
    }
  }

  udp.begin(2016);
}

void loop() {
  delay(1000);
  int packetSize = udp.parsePacket();
  if (packetSize) {
    IPAddress remoteIp = udp.remoteIP();

    // read the packet into packetBufffer
    int len = udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }

    String request = packetBuffer;
    String reply;
    if (request.equals("REG_WAIT?")) {
      if (id.equals("")) {
        reply = "YES";
      } else {
        reply = "NO";
      }
    } else if (request.startsWith("REGISTER AS")) {
      if (id.equals("")) {
        id = request.substring(12);
        EEPROM.begin(256);
        String save = "id=" + id;
        for (int i = 0; i < save.length(); i++) {
          EEPROM.write(i, save[i]);
        }
        EEPROM.write(save.length(), '\0');
        EEPROM.commit();
        reply = "DONE";
      } else {
        reply = "ALREADY REGISTERED";
      }
    } else if (request.startsWith("OPEN ")) {
      if (request.substring(6).equals(id)) {
        Serial.println("OPEN");
      }
    } else if (request.startsWith("DELETE ")) {
      if(request.substring(8).equals(id)) {
        deleteMe = true;
      }
    }

    // send a reply, to the IP EEPROMaddress and port that sent us the packet we received
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(reply.c_str());
    udp.endPacket();
  }

  if (digitalRead(2) == HIGH || deleteMe) {
    EEPROM.begin(256);
    EEPROM.write(0, '\0');
    EEPROM.commit();
    ESP.restart();
  }
}
