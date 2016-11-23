/******************************************************************************

WiFiDice

******************************************************************************/
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ESP8266TrueRandom.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>

#define HIT_HISTORY_COUNT 21

// MDNS name.  Allows you to connect to rng.local in the web browser
// instead of remembering the IP
const char* mDNSid   = "rng";
// The server's AP name
const char* AP_NAME = "RNG";
// AP password
const char* AP_PASS = "let'sRollIt";

uint8_t AP_CHANNEL = 3;
uint8_t AP_HIDDEN = 0;

// to send toss number to web gui
WebSocketsServer webSocket = WebSocketsServer(81);
// to serve html/js web gui
ESP8266WebServer server(80);

uint32_t tossHistory[HIT_HISTORY_COUNT];

uint8_t upperBound = 0;

int rnd = 0;

// shift all previous tosses one to the right
void shiftTossHistory() {
	for (int i=HIT_HISTORY_COUNT-1; i>0; i--) {
		if (tossHistory[i-1]) {
			tossHistory[i] = tossHistory[i-1];
		}
	}
}

// identify file by extension and return appropriate content-type
String getContentType(String filename) {

    if (server.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "text/plain\r\nCache-Control: public, max-age=3600";
    return "text/plain";
}

bool handleFileRead(String path) {
    Serial.println("handleFileRead: " + path);
    if (path.endsWith("/")) path += "index.htm";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
        if (SPIFFS.exists(pathWithGz))
            path += ".gz";
        File file = SPIFFS.open(path, "r");
        size_t sent = server.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

//format bytes
String formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes)+"B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes/1024.0)+"KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes/1024.0/1024.0)+"MB";
    } else {
        return String(bytes/1024.0/1024.0/1024.0)+"GB";
    }
}

//packing data into json string and sending it to all connected clients
void wsSendJson() {
	// At some point this 1000 byte buffer might not be large enough
	StaticJsonBuffer<1000> jsonBuffer;

	JsonObject& root = jsonBuffer.createObject();
	root["RN"] = rnd;//totalTargets;
  root["UB"] = upperBound;
	//creating array of historical toss data
  JsonArray& tossHist = root.createNestedArray("tossHist");
	for (int i=0; i<HIT_HISTORY_COUNT; i++) {
		if (tossHistory[i]) {
			tossHist.add(tossHistory[i]);
		}
	}
	String output;
	// output is tinysized.
	root.printTo(output);
	// This one will retain formatting
	//root.prettyPrintTo(output);
	webSocket.broadcastTXT(output);
}


// Main websocket handle
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
    break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
				// send message to client
				webSocket.sendTXT(num, "[SERVER]Connected");
      }
    break;

    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\r\n", num, payload);
			// checking for magic char
      if (payload[0] == 'R') {
				char cPayload[length];
				// copying the payload
				memcpy(cPayload, payload, length);
				// parsing the payload (value for upper bound)
				String uBound;
				int i = 1;
				while (cPayload[i] != '*') {
					uBound += cPayload[i];
					i++;
				}
				//setting upper bound for rng accordingly
        upperBound = uBound.toInt();
        Serial.println(upperBound);
				// starting random number generator
				Serial.println("start RNG...");
        rnd = ESP8266TrueRandom.random(0, upperBound);
        Serial.println(rnd);
				//shift toss history by one
        shiftTossHistory();
				//insert new random number in toss history
        tossHistory[0] = rnd;

      }
			//send data to web gui
      wsSendJson();
    break;

    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
    hexdump(payload, length);
    break;
// send message to client
// webSocket.sendBIN(num, payload, length);
  }
}


// some basic WiFi setup
void setupWiFi()
{
  WiFi.mode(WIFI_AP);
	Serial.println("Starting AP");
  WiFi.softAP(AP_NAME, AP_PASS, AP_CHANNEL, AP_HIDDEN);
	Serial.println("Starting WS");
	//starting webSocket
  webSocket.begin();
	//register WebSocket Callback event
  webSocket.onEvent(webSocketEvent);
}


void setup()
{
  Serial.begin(115200);
  setupWiFi();
  // Init our file system
  int test = SPIFFS.begin();
  Serial.println(test);
  delay(100);
  {
      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
          String fileName = dir.fileName();
          size_t fileSize = dir.fileSize();
          Serial.printf("FS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
      }
      Serial.printf("\n");
  }
  // Setup 404 handle on the web server
  server.onNotFound([]() {

    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
    });

  // Start web server
	server.begin();
  Serial.println("HTTP server started");

  // Add service to MDNS
	if (!MDNS.begin("nerf", WiFi.localIP())) {
		Serial.println("Error setting up MDNS responder!");
		// If we get here we spinlock and never recover.  Something more graceful
		// should probably happen
		while(1) {
			delay(1000);
		}
	}
	Serial.println("mDNS responder started");
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
}


void loop()
{
	webSocket.loop();
  server.handleClient();
}
