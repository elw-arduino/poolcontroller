#include <Ticker.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
extern "C" {
#include "user_interface.h"
}

ESP8266WebServer server(80);
//--------------- Wifi Configuration ------------------------
const char* ssid = "PACE629";
const char* pass = "0282735560";
IPAddress staticIP(192, 168, 1, 91);
IPAddress gateway(192, 168, 1, 254);
IPAddress subnet(255, 255, 255, 255);
//-----------------------------------------------------------

//Stored Temp value to send to seeds
signed int temp = 0;
//--------------- Seeds Timer -------------------------------
//Software timer for updating seeds
Ticker seedsTimer;
//Trigger for seeds request
bool request = false;

void seedsRequests(){
  request = true;
}
//-----------------------------------------------------------

void in() {
  if(server.hasArg("t")){
    temp = server.arg("t").toInt();
  }
  server.send(200, "text/html", String(temp));
}

void mainPage(){
  String s = "<h2>Eric's Pool Controller Relay</h2><br />";
  s += "Current Pool Temperature: "+String(temp)+"<br />";
  s += "<form action='/in'>";
  s += "<label>Update Pool Temperature (&degC):</label>";
  s += "<input type='number' min='-32768' max='32767' name='t' required /><br />";
  s += "<input type='submit' />";
  server.send(200, "text/html", s);
}

void setup() {
  
  Serial.begin(9600);
  Serial.println();

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to " + String(ssid));
  WiFi.config(staticIP,gateway,subnet);
  WiFi.hostname("PoolControlerRelay");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  server.on("/in", in);
  server.on("/", mainPage);
  server.begin();
  Serial.println("HTTP server started");
  seedsTimer.attach(60,seedsRequests);

}

void loop() {
  
  static unsigned long updateTemp = 0;
  
  server.handleClient();
  if(request){
    HTTPClient http;
    http.begin("heritageseeds.ca",80,"/app/pool/in.php?t="+String(temp));
    int httpCode = http.GET();
    Serial.println("Status:"+String(httpCode));
    if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(payload);
    }
    http.end();
    Serial.println("Seeds Temp Updated to "+String(temp)+"℃");
    http.begin("heritageseeds.ca",80,"/app/pool/scheduler.php");
    httpCode = http.GET();
    Serial.println("Status:"+String(httpCode));
    if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(payload);
    }
    http.end();
    Serial.println("Seeds Scheduler Updated");
    request = false;
  }
  
}
