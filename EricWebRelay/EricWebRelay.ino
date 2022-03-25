#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Ticker.h>
extern "C" {
#include "user_interface.h"
}


// Override modes
#define normal 0x00
#define ON 0x01
#define OFF 0x02
#define OffTillMorning 0x03

// Sync settings
//If the difference between the last sync and the current millis() is > than this constant the relay is considered to be out of sync
#define OUTOFSYNC 900000 //15 minutes

ESP8266WebServer server(80);
WiFiClient client;
HTTPClient http;

//--------------- Wifi Configuration ------------------------
// Set WiFi credentials
#define WIFI_SSID "BELL514"
#define WIFI_PASS "59716674"
 
// Set AP credentials
#define AP_SSID "PoolNet"
#define AP_PASS "68Dunbar"
//-----------------------------------------------------------

//----------------- Controller Sync Info --------------------
signed int temp = 0;
unsigned long PUMPStarttime = 0;
unsigned long PUMPRunTime = 0;
unsigned long timePumpOnToday = 0;
boolean PumpisOn = false;
byte Override;
unsigned long lastUpstreamSync = 0;
unsigned long lastDownstreamSync = 0;
boolean downstreamSyncRequired = false;
String downstreamSyncParams = "";
int lastDownstreamSyncResult = 0;
bool controllerConnected = false;
//-----------------------------------------------------------

//--------------- Seeds Timer -------------------------------
//Software timer for updating seeds
Ticker seedsTimer;
//Trigger for seeds request
bool request = false;

void seedsRequests(){
  request = true;
}
//-----------------------------------------------------------

void handleNotFound() {
  String message = "URI Not Found\n\n";
  message += "Target URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  Serial.print(message);
  server.send(404, "text/plain", message);
}

void in() {
  if(server.hasArg("t")){
    temp = server.arg("t").toInt();
  }
  if(server.hasArg("override")){
    Serial.println("Controller Sync Recieved");
  }
  if(downstreamSyncRequired){
    //The Relay needs to sync settings with the controller.
    //Don't allow the request to override the settings
    goto done;
  }
  if(server.hasArg("override")){
    Override = server.arg("override").toInt();
    lastUpstreamSync = millis();
  }
  if(server.hasArg("pump")){
    PumpisOn = server.arg("pump")?true:false;
    lastUpstreamSync = millis();
  }
  if(server.hasArg("start")){
    PUMPStarttime = server.arg("start").toInt();
    lastUpstreamSync = millis();
  }
  if(server.hasArg("run")){
    PUMPRunTime = server.arg("run").toInt();
    lastUpstreamSync = millis();
  }
  if(server.hasArg("ran")){
    timePumpOnToday = server.arg("ran").toInt();
    lastUpstreamSync = millis();
  }
  done:
  server.send(200, "text/html", String(temp));
}

void mainPage(){
  String s = "<h2>Eric's Pool Controller Relay</h2><br />";
  s += "Current Pool Temperature: "+String(temp)+"<br />";
  s += "<form action='/in'>";
  s += "<label>Update Pool Temperature (&degC):</label>";
  s += "<input type='number' min='-32768' max='32767' name='t' required /><br />";
  s += "<input type='submit' /></form>";
  if(controllerConnected){
    s += "<a href='/controller'>Acess Controller</a>";
    s += "<div style='color:green'><h3>Controller Connected</h3></div>";
  }
  else{
    s += "<div style='color:red'><h3>Controller NOT Connected</h3></div>";
  }
  server.send(200, "text/html", s);
}

void controller() {
  if(!controllerConnected){
    server.send(504, "text/html", "<h3>Controller Not Connected</h3>");
    return;
  }
  String s = "<html><center><head><meta content='yes' name='apple-mobile-web-app-capable' /><meta content='minimum-scale=1.0, width=device-width, maximum-scale=0.6667, user-scalable=no' name='viewport' /> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=EmulateIE7\"/><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /><title>Pool Pump Timer</title></head><body>";
  s += "<h2 onclick='location.reload()'>Pool Pump Timer</h2>";
  s += "<form action='/accept'>Pool pump is <span style='color:red'>";
  if(PumpisOn){
    s += "ON";
  }
  else{
    s += "OFF";
  }
  s += "</span><br />Pool Temperature is: "+String(temp)+"&deg;C<br />";
  s += "Pump will start running at: "+formatTime(PUMPStarttime)+"<br />";
  s += "Pump will run for <a style='color:red;text-decoration:none' href='/setTime'>"+formatTime(PUMPRunTime)+"</a> hours a day<br />";
  s += "The Pool Pump has Run for: "+formatTime(timePumpOnToday);
  s += "<br />Override state:<select name=override size=1>";
  switch(Override) {
    case normal:
      s += "<option selected />normal<option />ON<option />OFF<option />Off Till Morning";
      break;
    case ON:
      s += "<option />normal<option selected />ON<option />OFF<option />Off Till Morning";
      break;
    case OFF:
      s += "<option />normal<option />ON<option selected />OFF<option />Off Till Morning";
      break;
    case OffTillMorning:
      s += "<option />normal<option />ON<option />OFF<option selected />Off Till Morning";
      break;
  }
  s += "</select><br /><fieldset style='border:1px none; display:none' id='timedInfo' disabled><input type='number' id='timeFormat' min='0.5' step='0.5' required/>";
  s += "<select name=timeFormat size=1 id='failSafe'onfocus='timeFormats();' onchange='doUpdateForm();' required><option selected />-- Select Time Format --<option id='sec' />Seconds<option id='min' />Minutes<option id='hour' />Hours</select><span id='myRadControl' style='display:none'>";
  s += "<br />Return To:<br /><input type='radio' name='return'value='Normal' required /> Normal (Adds to the normal time)<br />";
  s += "<input type='radio' name='return'value='Off Till Morning' required /> Off Till Morning (Overrides the normal time)</span></fieldset>";
  s += "<br /><input id='submit'type=submit />";
  s += "<script type='text/javascript'>";
  s += "function timeFormats(){var rawSel = document.getElementById('timeFormat').value;var sel = parseInt(rawSel);if(rawSel != ''){document.getElementById('min').disabled = false;document.getElementById('hour').disabled = false;if(Math.round(sel) == parseFloat(rawSel)){document.getElementById('sec').disabled = false;}else{document.getElementById('sec').disabled = true;}}else{document.getElementById('sec').disabled = true;document.getElementById('min').disabled = true;document.getElementById('hour').disabled = true;}}function doUpdateForm(){var sel = document.getElementById('mySelect').value;if( sel == 'Timed' ) {document.getElementById('timedInfo').style.display = 'inline';document.getElementById('myRadControl').style.display = 'inline';document.getElementById('timedInfo').disabled = false;if(document.getElementById('failSafe').value == '-- Select Time Format --' && sel == 'Timed'){document.getElementById('submit').disabled = true;}else{document.getElementById('submit').disabled = false;}}else {document.getElementById('timedInfo').style.display = 'none';document.getElementById('myRadControl').style.display = 'none';document.getElementById('timedInfo').disabled = true;document.getElementById('submit').disabled = false;}}";
  s += "</script></form>";
  s += "<a href='http://catherapyservices.ca/app/pool/scheduler.php'>Pool Scheduler Page</a><br /><a href='http://catherapyservices.ca/app/pool/edit.php'>Pool Edit Page</a>";
  done:
  server.send(200, "text/html", s);
}

void configure() {
  if(!controllerConnected){
    server.send(504, "text/html", "<h3>Controller Not Connected</h3>");
    return;
  }
  String s = "<html><head><meta content='yes' name='apple-mobile-web-app-capable' /><meta content='minimum-scale=1.0, width=device-width, maximum-scale=0.6667, user-scalable=no' name='viewport' /> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=EmulateIE7\"/><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /><title>Pool Pump Timer</title></head><body><center><div style='border:1px solid black'><form action=accept>";
  s += "Change Pump Run Time to:<br />";
  s += "<select name=Pumpruntime size=1>";
  s += "<option selected value= '14400'>4 Hours</option><option value= '7200'>2 Hours</option><option value= '3600'>1 Hour</option><option value= '21600'>6 Hours</option>";
  s += "</select><br /><br /><input type=submit />";
  s += "</form></div><br /><div style='border:1px solid black'><form action=accept.html>";
  s += "Change Pump Start Time to:";
  s += "<br /><input type=number min='0' max='23.5' step='0.5'name=Pumpstarttime size=1 required /><br />24 hour time and only hours must be put in. (time format: HH)<br />";
  s += "<input type=submit /></form></div></center></body></html>";
  server.send(200, "text/html", s);
}

void cmd(){
  if(!controllerConnected){
    server.send(504, "text/html", "<h3>Controller Not Connected</h3>");
    return;
  }
  String s = "<html><head><meta content='yes' name='apple-mobile-web-app-capable' /><meta content='minimum-scale=1.0, width=device-width, maximum-scale=0.6667, user-scalable=no' name='viewport' /> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=EmulateIE7\"/><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /><title>Pool Pump Timer</title></head><body><center>";
  if(!downstreamSyncRequired){
    for(int i = 0;server.args();i++){
      if(downstreamSyncParams != ""){
        downstreamSyncParams += "&";
      }
      downstreamSyncParams += server.argName(i)+"="+server.arg(i);
    }
  }
  if(lastDownstreamSyncResult > 0){
    if(lastDownstreamSyncResult == 200){
      s += "<h2>Command Accepted</h2>";
    }
    else{
      s += "<h2>Command Not Accepted</h2>";
    }
  }
  else if(!downstreamSyncRequired){
    s += "<h2>Waiting for Sync</h2>";
    downstreamSyncRequired = true;
  }
  else{
    s += "<h2>Command Not Accepted</h2>";
  }
  s += "<a style='color:black;text-decoration:none' href='/controller'><div style='border:1px solid black;width:100px'>Return to<br />Main Page</div></a>";
  s += "</center></body></html>";
  server.send(200, "text/html", s);
}

void setup() {
  
  Serial.begin(9600);
  Serial.println();
  
  WiFi.hostname("PoolControlerRelay");

  // Begin Access Point
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
 
  // Begin WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
 
  // Connecting to WiFi...
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
 
  // Connected to WiFi
  Serial.println();
  Serial.println("Connected!");
  Serial.print("IP address for network ");
  Serial.print(WIFI_SSID);
  Serial.print(" : ");
  Serial.println(WiFi.localIP());
  Serial.print("IP address for network ");
  Serial.print(AP_SSID);
  Serial.print(" : ");
  Serial.println(WiFi.softAPIP());
  
  server.on("/in", in);
  server.on("/", mainPage);
  server.on("/setTime",configure);
  server.on("/accept",cmd);
  server.on("/controller",controller);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  seedsTimer.attach(60,seedsRequests);
  
}

void loop() {
  
  static unsigned long updateTemp = 0;
  
  server.handleClient();
  if(request){
    http.begin(client,"catherapyservices.ca",80,"/pool/in.php?auth=relay&t="+String(temp));
    int httpCode = http.GET();
    Serial.println("Status:"+String(httpCode));
    http.end();
    Serial.println("Seeds Temp Updated to "+String(temp)+"℃");
    request = false;
  }
  if(downstreamSyncRequired && controllerConnected){
    http.begin(client,"192.168.1.90",80,"/accept.html?"+downstreamSyncParams);
    lastDownstreamSyncResult = http.GET();
    if(lastDownstreamSyncResult > 0){
      lastDownstreamSync = millis();
      downstreamSyncParams = "";
      downstreamSyncRequired = false;
    }
  }
  IPAddress address;
  struct station_info *stat_info;
  struct ip4_addr *IPaddress;
  stat_info = wifi_softap_get_station_info();
  bool Connected = false;
  while(stat_info != NULL){
    IPaddress = &stat_info->ip;
    if(address.toString().c_str() == "192.168.1.90"){
      Connected = true;
    }
    stat_info = STAILQ_NEXT(stat_info, next);
  }
  controllerConnected = Connected;
}

/*
 *  Take a time in seconds and build a HH:MM time format
 *  for display perpuses.  Returns static char * with the
 *  formated time string.
 */
String formatTime (unsigned long T) {
  String timeString = "";
  byte hours, minutes;

  hours = (byte)((T%86400)/3600);
  minutes = (byte)((T/60)%60);
  
  timeString += num2char (hours/10);
  timeString += num2char (hours%10);
  timeString += ':';
  timeString += num2char (minutes/10);
  timeString += num2char (minutes%10);

  return (timeString);
}

/*
 *  Return the charatre for the given number given.
 *  The given number must be 0-9, else 'e' is returned.
 */
const char num2char (byte num) {
  switch (num) {
  case 0 : return ('0');
  case 1 : return ('1');
  case 2 : return ('2');
  case 3 : return ('3');
  case 4 : return ('4');
  case 5 : return ('5');
  case 6 : return ('6');
  case 7 : return ('7');
  case 8 : return ('8');
  case 9 : return ('9');
  default: return ('e');
  }
}
