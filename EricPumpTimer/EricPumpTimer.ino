#include <EEPROM.h>
#include <WiServer.h>
#include <Time.h>
#include <Wire.h>
#include <DS1307RTC.h>
#include <Thermistor.h>
#include <avr/wdt.h>

//Consts
#define VERSION 1.5
#define extraInfo "Beta"
#define WHOAREWE "Pool Pump Timer"
#define Error404 "ERROR 404"
#define RelayPin 48
#define IndicatorLED 9
#define ErrorLED 8
#define UpdateHowOften 60
#define unknown 0xFFF9
#define ONE_SECOND 1UL
#define normal 0x00
#define ON 0x01
#define OFF 0x02
#define OffTillMorning 0x03
#define Timed 0x04
#define STOREDSTATE 0x20
#define STOREDruntime 0x21  // Needs 4 bytes next avalible byte is 25
#define STOREDtime 0x25  // Needs 4 bytes next avalible byte is 29
#define STOREDStarttime 0x29  // Needs 4 bytes next avalible byte is 2D
#define STOREDExtraruntime 0x2D // Needs 4 bytes next avalible byte is 31
#define STOREDExtratime 0x31 // Needs 4 bytes next avalible byte is 36
#define STOREDReturnTo 0x36
#define STOREDUnit 0x37

//Macros
#define ReadStoredTime(Adr) (unsigned long)(((unsigned long)EEPROM.read(Adr)<<24)+((unsigned long)EEPROM.read(Adr+1)<<16)+((unsigned long)EEPROM.read(Adr+2)<<8)+(unsigned long)EEPROM.read(Adr+3))
#define StoreTime(Adr,Num) EEPROM.write(Adr,(byte)((Num&0xFF000000)>>24));EEPROM.write(Adr+1,(byte)((Num&0x00FF00)>>16));EEPROM.write(Adr+2,(byte)((Num&0x0000FF00)>>8));EEPROM.write(Adr+3,(byte)(Num&0x000000FF))

// Globel vareables
unsigned int Days = 0;
unsigned long PUMPStarttime;
unsigned long PUMPRunTime;
unsigned long timePumpOnToday;
boolean PumpisOn = false;
String PoolTempData;
boolean WeHaveWiFi = false;
byte Override;
boolean Blink = false;
boolean Blink_error = false;
boolean Startup = true;
boolean cmdactive = false;
unsigned int Returnto;
unsigned long ExtraRunTime;
unsigned long ExtraRanTime;
int Unit;

// Create Thermistor object
Thermistor PoolTemp = Thermistor (0,30000,298.15,110000UL,3997, "Pool Temperature probe");

// Function Defs
void setup ();
void loop ();
void startPump ();
void stopPump ();
char *formatTime (unsigned long);
const char num2char (byte);
boolean sendHTMLpages (char *);
int cmdToInt (String cmd);
String overrideMode (int mode);
void updateUnit (int newUnit);
String sendHTMLfooter ();

// Set up for sending pool tempature to Eric's ESP8266 Relay
unsigned char PoolTemp_ip[] = {192,168,1,91}; // relay
GETrequest PoolTempUpdate (PoolTemp_ip, 80, "192.168.1.91", "");
char PoolTempURL[] = {"/in?t="};

const char PROGMEM htmlHeader[] = "<html><head><style>#unit {display:inline;};</style><meta content='yes' name='apple-mobile-web-app-capable' /><meta content='minimum-scale=1.0, width=device-width, maximum-scale=0.6667, user-scalable=no' name='viewport' /> <meta http-equiv=\"X-UA-Compatible\" content=\"IE=EmulateIE7\"/><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /><title>Pool Pump Timer</title></head><center><body><h2 onclick='location.reload()'>Pool Pump Timer</h2>";
const char PROGMEM htmlReturntopage[] = "<a style=\"color:black;text-decoration:none\" href=\"Eric.html\"><div style=\"border:1px solid black;width:100px\">Return to<br />Main Page</div></a>";
//---------------------------------------------------------------------------
// Wireless configuration parameters
#define WIRELESS_MODE_INFRA 1
unsigned char local_ip[] = {192,168,1,90};	// IP address of WiShield
unsigned char gateway_ip[] = {192,168,1,254};	// router or gateway IP address
const char PROGMEM ssid[] = {"PACE629"};	// max 32 bytes
// WPA/WPA2 passphrase
const char PROGMEM security_passphrase[] = {"0282735560"};	// max 64 characters
unsigned char subnet_mask[] = {255,255,255,0};	// subnet mask for the local network

unsigned char security_type = 3;	// 0 - open; 1 - WEP; 2 - WPA; 3 - WPA2

// WEP 128-bit keys
// sample HEX keys
const unsigned char PROGMEM wep_keys[] = {};

// setup the wireless mode
// infrastructure - connect to AP
// adhoc - connect to another WiFi device
unsigned char wireless_mode = WIRELESS_MODE_INFRA;

unsigned char ssid_len;
unsigned char security_passphrase_len;
//---------------------------------------------------------------------------

void setup () {
	// start up serial communication
	// and print out some identification info
	Serial.begin (9600);
	Serial.print ("Project: ");
	Serial.print (WHOAREWE);
	Serial.print (" Version ");
	Serial.print (VERSION, 1);
        Serial.print (" ");
        Serial.println (extraInfo);

        PUMPStarttime = ReadStoredTime(STOREDStarttime);
        PUMPRunTime = ReadStoredTime(STOREDtime);
        timePumpOnToday = ReadStoredTime(STOREDruntime);
        Override = EEPROM.read(STOREDSTATE);
        ExtraRunTime = ReadStoredTime(STOREDExtraruntime);
        ExtraRanTime = ReadStoredTime(STOREDExtratime);
        Returnto = EEPROM.read(STOREDReturnTo);
        Unit = EEPROM.read(STOREDUnit);
        
	// setup a watch dog timer.
	wdt_enable (WDTO_8S);
	
	// Start up web server
	WiServer.init (sendHTMLpages);
	WiServer.enableVerboseMode (true);
	
	// set io pins for LEDs and Relay
	pinMode (RelayPin, OUTPUT);
	pinMode (ErrorLED, OUTPUT);
	pinMode (IndicatorLED, OUTPUT);
	
	// setup time library
	setSyncProvider (RTC.get);
	setSyncInterval (600); //every ten minutes

	// keep track of days
	Days = elapsedDays (now());

        StoreTime(STOREDruntime, 0);
        EEPROM.write(STOREDSTATE, normal);
        StoreTime(STOREDExtraruntime, 0);
        StoreTime(STOREDExtratime, 0);
        EEPROM.write(STOREDReturnTo, 0);

}

void loop () {
	static unsigned long PoolTempTimer = 0;
	static unsigned long delta_t = 0;
	static unsigned long error_t = 0;
        String str;

	// run web server tasks
	WeHaveWiFi = WiServer.server_task ();
        if (WeHaveWiFi)
          digitalWrite (ErrorLED, LOW);
        else
          digitalWrite (ErrorLED, HIGH);

	// calculate pool temperature
	PoolTemp.buildAve ();

	// send pool temperature to the relay
	if (WeHaveWiFi && now() - PoolTempTimer > UpdateHowOften) {
                Serial.println("Connecting to Relay");
		PoolTempData = PoolTempURL;
		PoolTempData += PoolTemp.AveTemperature ();
		PoolTempUpdate.setURL ((char *) PoolTempData.c_str());
		PoolTempUpdate.submit ();
		PoolTempTimer = now ();
                Serial.println("Disconnecting from Relay");
	}

      if (WeHaveWiFi) {
      if (!cmdactive) {
        Serial.println ("Command Terminal Activated");
        cmdactive = true;}
        if (Serial.available ()) {	// See if string is avalible
	str = Serial.readStringUntil ('\n');
        if (str.startsWith("setPumpTime")) {
          String str2 = str.substring(12);
          Serial.print ("Set timePumpOnToday to: ");
          Serial.println (str2);
          timePumpOnToday = str2.toInt();
          StoreTime(STOREDruntime, timePumpOnToday);
        }
        else if (str.startsWith("setOverride")) {
          switch (cmdToInt(str)) {
           case normal: {
             Override = normal;
             Serial.println ("Set Override to: Normal");
             break;
           }
           case OFF: {
             Override = OFF;
             Serial.println ("Set Override to: OFF");
             stopPump ();
             break;
           }
           case ON: {
             Override = ON;
             Serial.println ("Set Override to: ON");
             startPump ();
             break;
           }
           case OffTillMorning: {
             Override = OffTillMorning;
             Serial.println ("Set Override to: Off Till Morning");
             stopPump ();
             break;
           }
           case unknown: {
            Serial.println (Error404);
            Serial.print ("No Mode Called ");
            Serial.print (str.substring(13));
            Serial.println ("Found");
           }
          }
          EEPROM.write(STOREDSTATE, Override);
        }
        else if (str.startsWith("Override?")) {
         Serial.print ("Override = ");
         Serial.println (overrideMode(Override));
        }
        else if (str.startsWith("EEPROM")) {
         if (str.endsWith("STOREDruntime")) {
          Serial.println (ReadStoredTime(STOREDruntime));
         }
         else if (str.endsWith("STOREDSTATE")) {
          Serial.println (EEPROM.read(STOREDSTATE));
         }
        }
        else {
         Serial.println ("Command NOT Exepted");
        }
        }}

	// accumulate pump run time
	if (PumpisOn && now () - delta_t >= ONE_SECOND) {
                if (Override == Timed)
                  ExtraRanTime++;
                else
		  timePumpOnToday++;
		delta_t = now ();
                if (WeHaveWiFi) {
                  if (Blink)
                    Blink = false;
                  else
                    Blink = true;
                }
                else if (!WeHaveWiFi)
                    Blink = true;
	}
        else if (!PumpisOn)
          Blink = false;

	// catch a beginning of a new day
	if (Days != elapsedDays (now())) {
		Days = elapsedDays (now());
		if (Override == normal || Override == OffTillMorning){
		 	// if in Override over night don't clear pump run time.
                        if (!Startup)
			  timePumpOnToday = 0;
                        if (Override == OffTillMorning){
                         Override = normal; 
                        }
                }
	}

	// see if its time to start the pump,
	// But only if we are Override normal
	if (Override == normal) {
		if (elapsedSecsToday (now ()) >= PUMPStarttime &&
		 timePumpOnToday < PUMPRunTime)
			startPump ();
		// see if the pump is on and if its run long enough
		if (PumpisOn && timePumpOnToday >= PUMPRunTime)
			stopPump ();
	}
	else if (Override == Timed) {
		if (ExtraRanTime < ExtraRunTime)
			startPump ();
		// see if the pump is on and if its run long enough
		if (ExtraRanTime >= ExtraRunTime){
			stopPump ();
                        Override = Returnto;
                }
	}

        //check for error with probe
	if (PoolTemp.isError() && now () - error_t >= ONE_SECOND) {
		error_t = now ();
                  if (Blink_error)
                    Blink_error = false;
                  else
                    Blink_error = true;
        }
	
          digitalWrite (IndicatorLED, Blink ? HIGH : LOW);

           //Indicate if there is an Error with the probe
        if (WeHaveWiFi) {
            digitalWrite (ErrorLED, Blink ? HIGH : LOW);
          }
        
	// Reset the WDT, must be done at least every 8s
	wdt_reset ();
        Startup = false;
}

int cmdToInt(String cmd)
{
    String cmd2 = cmd.substring(12);
  
    if( cmd2.equalsIgnoreCase("normal") )  return(normal);
    if( cmd2.equalsIgnoreCase("OFF") )  return(OFF);
    if( cmd2.equalsIgnoreCase("ON") )  return(ON);
    if( cmd2.equalsIgnoreCase("OffTillMorning") )  return(OffTillMorning);
    if( cmd2.equalsIgnoreCase("Off Till Morning") )  return(OffTillMorning);
    if( cmd2.equalsIgnoreCase("Timed") )  return(Timed);
    return (unknown);
}

String overrideMode(int mode)
{
    switch (mode) {
      case normal: return ("Normal");
      case OFF: return ("Off");
      case ON: return ("On");
      case OffTillMorning: return ("Off Till Morning");
      case Timed: return ("Timed");
    }
}
/*
*      Update Unit
*
*      Called to Update the Unit of display for the Web Portal Interface
*      Check if its a valid number
*      Check if we are in Read Only mode
*      If its changed then update the EEPROM
*/
void updateUnit(int newUnit) {
  if (newUnit == 0 || newUnit == 1){
      if (Unit != newUnit){
        Unit = newUnit;
        EEPROM.write(STOREDUnit, Unit);
      }
  }
}

/*
 *	Start Pump
 *
 *	Called to start the pump
 *	Set pin PUMPRELAYPIN high
 *	Start pump run time counter
 */
void startPump () {
	digitalWrite (RelayPin, HIGH);
        if (!PumpisOn) Blink = true;
        PumpisOn = true;
}

/*
 *	Stop Pump
 *
 *	Called to stop the pump
 *	Set pin PUMPRELAYPIN low
 *	Stop pump run time counter
 */
void stopPump () {
	digitalWrite (RelayPin, LOW);
        if (PumpisOn) Blink = false;
	PumpisOn = false;
}

/*
 *	Take a time in seconds and build a HH:MM time format
 *	for display perpuses.  Returns static char * with the
 *	formated time string.
 */
char *formatTime (unsigned long T) {
	static char timeString[6];
	byte hours, minutes;

	hours = (byte) numberOfHours (T);
	minutes = (byte) numberOfMinutes (T);
	
	timeString[0] = (char) num2char (hours/10);
	timeString[1] = (char) num2char (hours%10);
	timeString[2] = (char) ':';
	timeString[3] = (char) num2char (minutes/10);
	timeString[4] = (char) num2char (minutes%10);
	timeString[5] = (char) '\0';

	return ((char *)timeString);
}

/*
 *	Return the charatre for the given number given.
 *	The given number must be 0-9, else 'e' is returned.
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

String sendHTMLfooter (){
	
	String s = "</body><footer>Project: ";
	s += WHOAREWE;
	s += " Version ";
	s += String(VERSION,1);
	s += " ";
	s += extraInfo;
        s += "<form action='go.html' id='unit'><input class='Url' name='url' value='' type='hidden'><input type='hidden' name='unit' value='C'>";
	switch (Unit){
        case 1:
        s += "<input type='submit' value='°C'/>";
        break;
        case 0:
        s += "<input type='submit' value='°C' disabled/>";
        break;
        }
        s += "</form><form action='go.html' id='unit'><input class='Url' name='url' value='' type='hidden'><input type='hidden' name='unit' value='F' />";
        switch (Unit){
        case 1:
        s += "<input type='submit' value='°F' disabled />";
        break;
        case 0:
        s += "<input type='submit' value='°F' />";
        break;
        }
        s += "</form><script>var u = location.href, s = u.split('/'), url; if(s[s.length - 1]) {url = s[s.length - 1]}else {url = s[s.length - 2];} var list = document.querySelectorAll('.Url'); var n; for (n = 0; n < list.length; ++n){list[n].value = url}</script></footer></center></html>";
	return (s);
	
}

/*
 *	Send html pages.
 *
 *	This function is called every time a http request is made of
 *	this device.  It is passed the URL part of the request. This 
 *	can be used to server different content (ie. pages) depending
 *	on the request.
 *	Returnes true if a valade url was recieved and sent. Or false
 *	if an invalade request was made.
 */
boolean sendHTMLpages (char *url) {
	// Block of code to be executed only once per page
	if (WiServer.firstCall ()) {
		if (strcmp (url, "/accept.html?override=normal") == 0) {
			Override = normal;
                }
		else if (strcmp (url, "/accept.html?override=ON") == 0) {
			Override = ON;
			startPump ();
		}
		else if (strcmp (url, "/accept.html?override=OFF") == 0) {
			Override = OFF;
			stopPump ();
		}
		else if (strcmp (url, "/accept.html?override=Off+Till+Morning") == 0) {
			Override = OffTillMorning;
			stopPump ();
		}
		else if (strncmp (url, "/accept.html?Pumpstarttime", 26) == 0) {
			String surl(url);
                        PUMPStarttime = surl.substring(27).toInt();
                        PUMPStarttime = PUMPStarttime * 3600;
                        StoreTime(STOREDStarttime,PUMPStarttime);
       		}
		else if (strncmp (url, "/accept.html?Pumpruntime", 24) == 0) {
			String surl(url);
                        PUMPRunTime = surl.substring(25).toInt();
                        StoreTime(STOREDtime,PUMPRunTime);
       		}
		else if (strncmp (url, "/accept.html?override=Timer", 27) == 0) {
                  String surl(url);
                  int i;
                  String snum;
                  int time;
                  int nextVar;
                  i = surl.indexOf("&time=");
                  if (i > 27) {
                    nextVar = surl.indexOf("&", i);
                    if (nextVar > 27)
                      snum = surl.substring(i+6, nextVar);
                    else
                      snum = surl.substring(i+6);
                    time = snum.toInt();
                  }
                  String txt;
                  i = surl.indexOf("&return=");
                  if (i > 27) {
                    nextVar = surl.indexOf("&", i);
                    if (nextVar > 27)
                      snum = surl.substring(i+8, nextVar);
                    else
                      snum = surl.substring(i+8);
                    Returnto = cmdToInt(snum);
                  }
       		}
	}
         /*
	 *	Main HTML page
	 */
	if (strncmp (url, "/Eric.html", 10) == 0) {
		WiServer.print_P (htmlHeader);
                WiServer.print ("Pool Timer Time: ");
                WiServer.print (formatTime (elapsedSecsToday (now ())));
		WiServer.print ("<form action=accept.html>Pool pump is <span style=\"color:red\">");
		WiServer.print (PumpisOn?"ON":"OFF");
		WiServer.print ("</span><br />");
                if (PoolTemp.isError ()) {
                  WiServer.print ("There is an Error with the ");
                  WiServer.print (PoolTemp.getIdentifierStr ());
                }
                else {
		  WiServer.print ("Pool Tempature is ");
		  WiServer.print (PoolTemp.AveTemperature (), DEC);
		  WiServer.print ("&deg;C");
                }
		WiServer.print ("<br />Pump will start running at <a style=\"color:red;text-decoration:none\" href=\"setTime.html\">");
		WiServer.print (formatTime (PUMPStarttime));
		WiServer.print ("</a><br />");
		WiServer.print ("Pump will run for <a style=\"color:red;text-decoration:none\" href=\"setTime.html\">");
		WiServer.print (formatTime (PUMPRunTime));
		WiServer.print ("</a> hours a day<br />");
                WiServer.print ("The Pool Pump has run for: ");
                WiServer.print (formatTime (timePumpOnToday));
		WiServer.print ("<br />Override state:");
                WiServer.print ("<select name=override size=1 id='Select' onchange='doUpdateForm(); style='display:inline'>");
                switch (Override) {
                case normal: 
		WiServer.print (F("<option selected />normal<option />ON<option />OFF<option />Off Till Morning<option />Timed"));
                break;
		case ON:
		WiServer.print (F("<option />normal<option selected />ON<option />OFF<option />Off Till Morning<option />Timed"));
                break;
                case OFF:
		WiServer.print (F("<option />normal<option />ON<option selected />OFF<option />Off Till Morning<option />Timed"));
		break;
                case OffTillMorning:
		WiServer.print (F("<option />normal<option />ON<option />OFF<option selected />Off Till Morning<option />Timed"));
		break;
                case Timed:
		WiServer.print (F("<option />normal<option />ON<option />OFF<option />Off Till Morning<option selected />Timed"));
		break;
		}
                WiServer.print (F("</select><br /><fieldset style='border:1px none; display:none' id='timedInfo' disabled><input type='number' id='timeFormat' min='0.5' step='0.5' required/>"));
                WiServer.print (F("<select name=timeFormat size=1 id='failSafe'onfocus='timeFormats();' onchange='doUpdateForm();' required><option selected />-- Select Time Format --<option id='sec' />Seconds<option id='min' />Minutes<option id='hour' />Hours</select><span id='myRadControl' style='display:none'>"));
                WiServer.print (F("<br />Return To:<br /><input type='radio' name='return'value='Normal' required /> Normal (Adds to the normal time)<br />"));
                WiServer.print (F("<input type='radio' name='return'value='Off Till Morning' required /> Off Till Morning (Overrides the normal time)</span></fieldset>"));
                WiServer.print (F("<br /><input id='submit'type=submit />"));
                WiServer.print (F("<script type='text/javascript'>"));
                WiServer.print (F("function timeFormats(){var rawSel = document.getElementById('timeFormat').value;var sel = parseInt(rawSel);if(rawSel != ''){document.getElementById('min').disabled = false;document.getElementById('hour').disabled = false;if(Math.round(sel) == parseFloat(rawSel)){document.getElementById('sec').disabled = false;}else{document.getElementById('sec').disabled = true;}}else{document.getElementById('sec').disabled = true;document.getElementById('min').disabled = true;document.getElementById('hour').disabled = true;}}function doUpdateForm(){var sel = document.getElementById('mySelect').value;if( sel == 'Timed' ) {document.getElementById('timedInfo').style.display = 'inline';document.getElementById('myRadControl').style.display = 'inline';document.getElementById('timedInfo').disabled = false;if(document.getElementById('failSafe').value == '-- Select Time Format --' && sel == 'Timed'){document.getElementById('submit').disabled = true;}else{document.getElementById('submit').disabled = false;}}else {document.getElementById('timedInfo').style.display = 'none';document.getElementById('myRadControl').style.display = 'none';document.getElementById('timedInfo').disabled = true;document.getElementById('submit').disabled = false;}}"));
                WiServer.print ("</script>");
		WiServer.print ("</form>");
                WiServer.print ("<a href='http://seeds.ca/app/pool/scheduler.php'>Pool Scheduler Page</a><br /><a href='http://seeds.ca/app/pool/edit.php'>Pool Edit Page</a>");
                WiServer.print (sendHTMLfooter());
		return true; //web page servered
	}
        if (strncmp (url, "/accept.html", 12) == 0) {
             WiServer.print_P (htmlHeader);
             WiServer.print ("<h2>Command Accepted</h2>");
	     WiServer.print_P (htmlReturntopage);
             WiServer.print (sendHTMLfooter());
	     return true;
        }
        if (strncmp (url, "/setTime.html", 12) == 0) {
             WiServer.print_P (htmlHeader);
             WiServer.print ("<div style=\"border:1px solid black\"><form action=accept.html>");
             WiServer.print ("Change Pump Run Time to:<br />");
             WiServer.print ("<select name=Pumpruntime size=1>");
             switch (PUMPRunTime) {
               case 14400:
                 WiServer.print (F("<option selected value= \"14400\">4 Hours</option><option value= \"7200\">2 Hours</option><option value= \"3600\">1 Hour</option><option value= \"21600\">6 Hours</option>"));
		break;
               case 7200:
                 WiServer.print (F("<option value= \"14400\">4 Hours</option><option selected value= \"7200\">2 Hours</option><option value= \"3600\">1 Hour</option><option value= \"21600\">6 Hours</option>"));
		break;
               case 3600:
                 WiServer.print (F("<option value= \"14400\">4 Hours</option><option value= \"7200\">2 Hours</option><option selected value= \"3600\">1 Hour</option><option value= \"21600\">6 Hours</option>"));
		break;
               case 21600:
                 WiServer.print (F("<option value= \"14400\">4 Hours</option><option value= \"7200\">2 Hours</option><option value= \"3600\">1 Hour</option><option selected value= \"21600\">6 Hours</option>"));
		break;
               default:
                 WiServer.print (F("<option selected value= \"14400\">4 Hours</option><option value= \"7200\">2 Hours</option><option value= \"3600\">1 Hour</option><option value= \"21600\">6 Hours</option>"));
		break;
             }
             WiServer.print ("</select><br /><br /><input type=submit />");
             WiServer.print ("</form></div><br /><div style=\"border:1px solid black\"><form action=accept.html>");
             WiServer.print ("Change Pump Start Time to:");
             WiServer.print ("<br /><input type=number name=Pumpstarttime size=1 required min=\"0\" max=\"23.5\" step=\"0.5\" /><br />24 hour time and only hours must be put in. (time format: HH)<br />");
             WiServer.print ("<input type=submit /></form></div>");
             WiServer.print (sendHTMLfooter());
	     return true;
        }
        /*
        * Mistyped Page Redirects
        *
        * If it doesn't meet any of the above requirements,
        * only then will we consider it to be a mistyped page and redirect to the main page
        */
        else {
          if (strcmp (url, "/") == 0) {        //This Redirect is also used to connect via seeds.ca/app/pool/controller.php
                  WiServer.print ("<head><meta http-equiv='refresh' content='0; url=Eric.html' /></head>");
                  return true;
          }
          if (strcmp (url, "/eric.html") == 0){
                  WiServer.print ("<head><meta http-equiv='refresh' content='0; url=Eric.html' /></head>");
                  return true;
          }
        }
        
	return false; //web page not servered
}
