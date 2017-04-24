#include <WiServer.h>
#include <Time.h>
#include <Wire.h>
#include <DS1307RTC.h>
#include <Thermistor.h>
#include <avr/wdt.h>

#define VERSION 1.1
#define WHOAREWE "Pool Pump Timer"
#define RelayPin 48
#define IndicatorLED 9
#define ErrorLED 8
#define UpdateHowOften 60 /* send Temp data to Bob's server every 60sec */
#define ONE_SECOND 1UL
#define normal 0x00
#define ON 0x01
#define OFF 0x02

// Globel vereables
unsigned int Days;
unsigned long PUMPStarttime = 46800; // 1pm in sec
unsigned long PUMPRunTime = 14400; // 4 hours in sec
unsigned long timePumpOnToday = 0;
boolean PumpisOn = false;
String PoolTempData;
boolean WeHaveWiFi = false;
byte Override = normal;
int Blink;

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

// Set up for sending pool tempature to Bob's server
unsigned char PoolTemp_ip[] = {104,200,28,125}; // seeds.ca
GETrequest PoolTempUpdate (PoolTemp_ip, 80, "seeds.ca", "");
char PoolTempURL[] = {"/app/pool/in.php?t="};

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
	Serial.println (VERSION, 1);

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
}

void loop () {
	static unsigned long PoolTempTimer = 0;
	static unsigned long delta_t = 0;

	// run web server tasks
	WeHaveWiFi = WiServer.server_task ();

	// calculate pool temperature
	PoolTemp.buildAve ();

	// send pool temperature to Bob's server
	if (WeHaveWiFi && now() - PoolTempTimer > UpdateHowOften) {

		PoolTempData = PoolTempURL;
		PoolTempData += PoolTemp.AveTemperature ();
		PoolTempUpdate.setURL ((char *) PoolTempData.c_str());
		PoolTempUpdate.submit ();
		PoolTempTimer = now ();
	}

	// accumulate pump run time
	if (PumpisOn && now () - delta_t >= ONE_SECOND) {
		timePumpOnToday++;
		delta_t = now ();
                if (WeHaveWiFi) {
                      switch (Blink) {
                        case 0: {
                         Blink = 1;
                         break;
                        }
                        case 1: {
                          Blink = 0;
                          break;
                        }
                      }
                }
                else {
                    Blink = 1;
                }
	}

	// catch a beginning of a new day
	if (Days != elapsedDays (now())) {
		Days = elapsedDays (now());
		if (Override == normal)
		 	// if in Override over night don't clear pump run time.
			timePumpOnToday = 0;
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

        switch (Blink) {
          case 0:
            digitalWrite (IndicatorLED, LOW);
            break;
          case 1:
            digitalWrite (IndicatorLED, HIGH);
            break;
        }
        
	// Reset the WDT, must be done at least every 8s
	wdt_reset ();
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
	PumpisOn = true;
        Blink = 1;
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
	PumpisOn = false;
        Blink = 0;
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
		if (strcmp (url, "/Eric.html?override=normal") == 0)
			Override = normal;
		else if (strcmp (url, "/Eric.html?override=ON") == 0) {
			Override = ON;
			startPump ();
		}
		else if (strcmp (url, "/Eric.html?override=OFF") == 0) {
			Override = OFF;
			stopPump ();
		}
	}
	/*
	 *	Main HTML page
	 */
	if (strncmp (url, "/Eric.html", 10) == 0) {
		WiServer.print ("<html><head></head><body>");
		WiServer.print ("<h2>Pool Pump Timer</h2>");
                WiServer.print ("Pool Timer Time: ");
                WiServer.print (formatTime (elapsedSecsToday (now ())));
		WiServer.print ("<form>Pool pump is <span style=\"color:red\">");
		WiServer.print (PumpisOn?"ON":"OFF");
		WiServer.print ("</span><br />");
		WiServer.print ("Pool Tempature is ");
		WiServer.print (PoolTemp.AveTemperature (), DEC);
		WiServer.print ("&deg;C<br />");
		WiServer.print ("Pump will start running at ");
		WiServer.print (formatTime (PUMPStarttime));
		WiServer.print ("<br />");
		WiServer.print ("Pump will run for ");
		WiServer.print (formatTime (PUMPRunTime));
		WiServer.print (" hours a day<br />");
                WiServer.print ("The Pool Pump has run for: ");
                WiServer.print (formatTime (timePumpOnToday));
		WiServer.print ("<br />Override state:");
                WiServer.print ("<select name=override size=1>");
                switch (Override) {
                case normal: 
		WiServer.print ("<option selected />normal<option />ON<option />OFF");
                break;
		case ON:
		WiServer.print ("<option />normal<option selected />ON<option />OFF");
                break;
                case OFF:
		WiServer.print ("<option />normal<option />ON<option selected />OFF");
		break;
		}
		WiServer.print ("</select>");
                WiServer.print ("<input type=submit />");
                WiServer.print ("<br />Project: ");
                WiServer.print (WHOAREWE);
		WiServer.print (" Version ");
		WiServer.print (VERSION, 1);
		WiServer.print ("</form></body></html>");
		return true; //web page servered
	}
	return false; //web page not servered

}
