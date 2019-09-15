#include <memory>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

#include "http_page.h"

//enum definition to keep track of program state
enum State {UNDEFINED, GATEWAY, MASTER};

//Global variables to keep track of program state
State state = UNDEFINED;
bool gatewaySetUp;
bool masterSetUp;
bool discovered;

//Pointers to the server objects. Must be initialized at a later time
std::unique_ptr<ESP8266WebServer> gateway, master;

HTTPClient client;

//Define configuration variables necessary to initialize the Access Point mode
IPAddress local_IP = IPAddress(192,168,20,1);
IPAddress gateway_IP = IPAddress(192,168,20,1);
IPAddress subnet = IPAddress(255,255,255,0);
IPAddress broadcastAddress;

//UDP ports
#define MASTER_PORT 2004
#define SLAVE_PORT 2016

//Variable that holds id
String id;

WiFiUDP udp;

void setup() {
	Serial.begin(9600);
	WiFi.mode(WIFI_AP_STA);
	gatewaySetUp = false;
	masterSetUp = false;
	discovered = false;

	readIDFromEEPROM();

	for(int i = 0; i < 10; i++) {
		Serial.print("-");
		delay(500);
	}

	Serial.println();
}

void loop() {

	delay(1000);

	if(state == UNDEFINED) {
		if(WiFi.status() == WL_CONNECTED) {
			state = MASTER;
		} else {
			state = GATEWAY;
		}
	} else if(state == GATEWAY) {
		if(!gatewaySetUp) {
			setUpGateway();
		}

		gateway->handleClient();

		if(WiFi.status() == WL_CONNECTED) {
			cleanupGateway();
			state = MASTER;
			delay(500);
		}
	} else {
		if(!masterSetUp) {
			setUpMaster();
		}

		if(!discovered) {
			receivePacket();
		}

		master->handleClient();
		checkForMessage();
	}

	Serial.println("my id is " + id);

}

//Sets everything up for the board to serve as a gateway to allow the user to connect it to a network
void setUpGateway() {
	WiFi.softAP("BitLock");
	WiFi.softAPConfig(local_IP, gateway_IP, subnet);

	gateway = std::unique_ptr<ESP8266WebServer>(new ESP8266WebServer(80));

	gateway->on("/", gatewayHandleRoot);
	gateway->on("/connect", gatewayHandleConnect);
	gateway->begin();

	gatewaySetUp = true;
}

//Serves the webpage for the root page on the gateway webserver
void gatewayHandleRoot() {
	int nNetworks = WiFi.scanNetworks();

	String response = INDEX_HTML_PRELIST;

	for(int i = 0; i < nNetworks; i++) {
		response += "<option value=\"";
		response += WiFi.SSID(i);
		response += "\">";
		response += WiFi.SSID(i);
		response += "</option>\n";
	}

	response += INDEX_HTML_POSTLIST;

	gateway->send(200, "text/html", response);
}

//Handles the form data to connect to the wifi networks
void gatewayHandleConnect() {
	if(!gateway->hasArg("network") || !gateway->hasArg("password")) {
		gateway->sendHeader("Location", "/", true);
		gateway->send(302, "text/html", "");
	} else {
		WiFi.begin(gateway->arg("network"), gateway->arg("password"));
		gateway->send(200);
	}
}

void cleanupGateway() {
	gateway->stop();

	WiFi.softAPdisconnect();
}

void setUpMaster() {
	WiFi.softAP("BitLockMesh", "bitlockmesh", 1, 1);

	delay(1000);

	master = std::unique_ptr<ESP8266WebServer>(new ESP8266WebServer(WiFi.localIP(), 80));

	master->on("/", handleMasterRoot);
	master->on("/devices/", handleMasterDevices);
	master->on("/reset/", handleMasterReset);
	master->begin();

	broadcastAddress = (uint32_t)WiFi.softAPIP() | ~((uint32_t)subnet);
	udp.begin(MASTER_PORT);

	masterSetUp = true;
}

void handleMasterRoot() {

	if(master->method() == HTTP_GET) {
		if(id.length() == 0) {
			master->send(200, "text/plain", "ITS A ME, BITLOCK!");
		} else {
			master->send(200, "text/plain", id);
		}
	} else if(master->method() == HTTP_POST) {
		if(id.length() == 0) {
			id = master->arg("plain");
			writeIDToEEPROM();
			master->send(200, "text/plain", "Registered");
		} else {
			master->send(403, "text/plain", "Already registered");
		}

	} else {
		master->send(405, "text/plain", "Method not allowed");
	}
}

void handleMasterDevices() {
	char message[100];
	message[0] = 0;
	
	if(master->method() == HTTP_GET) {
		strcat(message, "REG_WAIT?");
		broadcastPacketToSlaves((uint8_t *) message, strlen(message) + 1);

		String response = receivePacket();

		if(response.length() == 0) {
			master->send(200, "text/plain", "No devices reachable");
		} else if(response.equals("NO")) {
			master->send(200, "text/plain", "No devices waiting for register");
		} else if(response.equals("YES")) {
			master->send(200, "text/plain", "Device waiting to be registered");
		} else {
			master->send(200, "text/plain", "Unable to understand response. Please try again");
		}	
	} else if(master->method() == HTTP_POST) {
		strcat(message, "REGISTER AS ");
		String slaveId = master->arg("plain");
		strcat(message, slaveId.c_str());

		broadcastPacketToSlaves((uint8_t *)message, strlen(message) + 1);

		String response = receivePacket();

		if(response.length() == 0) {
			master->send(200, "text/plain", "No devices reachable");
		} else if(response.equals("DONE")) {
			master->send(200, "text/plain", "DONE");
		} else {
			master->send(200, "text/plain", "Unable to understand response. Please try again");
		}		
	} else {
		master->send(405, "text/plain", "Method not allowed");
	}
}

void handleMasterReset() {
	EEPROM.begin(256);
	EEPROM.write(0, '\0');
	EEPROM.commit();
}

void broadcastPacketToSlaves(uint8_t* buf, uint8_t length) {
	udp.beginPacket(broadcastAddress, SLAVE_PORT);
	udp.write(buf, length);
	udp.endPacket();
}

String receivePacket() {
	char buffer[200];
	int packetSize;
	while((packetSize = udp.parsePacket()) == 0) delay(50);

	int length = udp.read(buffer, 200);
	if(length > 0) {
		buffer[length] = 0;
	}

	if(strcmp(buffer, "Finding Bitlock") == 0) {
		udp.beginPacket(udp.remoteIP(), udp.remotePort());
    	udp.write("You found me, lying on the floor");
    	udp.endPacket();
    	discovered = true;
    	return "";
	} else {
		return String(buffer);
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

void checkForMessage() {
	const char* host = "https://bitlock-api.herokuapp.com/devices/waiting/";

	String thumbprint = "08:3B:71:72:02:43:6E:CA:ED:42:86:93:BA:7E:DF:81:C4:BC:62:30";

	if (id.length() > 0 && client.begin(String(host) + id, thumbprint)) {
		int statusCode = client.GET();
		String response;
		if (statusCode > 0) {
			response = client.getString();
		}
		Serial.println(response);
		if (response.startsWith("OPEN")) {
			char message[100];
			response.toCharArray(message, 100);
			Serial.println(message);
			broadcastPacketToSlaves((uint8_t *)message, strlen(message) + 1);
		}
	}
}