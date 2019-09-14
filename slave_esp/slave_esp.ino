#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

const uint16_t UDPPort = 2016;
const uint16_t TCPPort = 2019;

WiFiUDP udp;
WiFiServer server(TCPPort);
WiFiClient master;

char packetBuffer[255];
String id;
bool deleteMe;
bool runServer;

void setup() {
  Serial.begin(9600);

  id = "";
  deleteMe = false;
  runServer = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin("BitLockMesh", "bitlockmesh");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println(".");
    delay(1000);
  }

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
  
  if(runServer) {
    if(master && master.connected()) {
      String request;
      if(master.available()) {
        request = master.readStringUntil('\n');
      }

      if(request.length() == 0) {

      } else if (request.equals("REG_WAIT?")){
        master.print(id.equals("") ? "YES\n" : id + "\n");
        master.flush();
      } else if (request.startsWith("REGISTER AS ")){
        String requestId = request.substring(13);
        if(id.equals("")) {
          id = "id+" + requestId;
          EEPROM.begin(256);
          for(unsigned int i = 0; i < id.length(); i++) {
            EEPROM.write(i, id[i]);
          }
          EEPROM.write(id.length(), '\0');
          EEPROM.commit();

          id = requestId;
          master.print("DONE\n");
        } else {
          master.print("FAIL\n");
        }
        master.flush();
      } else if(request.equals("OPEN DOOR")) {
        Serial.print("OPEN\n");
      } else if(request.equals("DELETE")){
        deleteMe = true;
      }
    } else {
      master = server.available();
    }
  } else {
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
      if (request.equals("Anyone there?")) {
        reply = "Yes";
        runServer = true;
        server.begin();
      }

      delay(500);
      // send a reply, to the IP and port that sent us the packet we received
      if (!reply.equals("")) {
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        char buffer[255];
        reply.toCharArray(buffer, 255);
        udp.write(buffer);
        udp.endPacket();
      }
    }
  }

  if (deleteMe) {
    EEPROM.begin(256);
    EEPROM.write(0, '\0');
    EEPROM.commit();
    ESP.restart();
  }
}
