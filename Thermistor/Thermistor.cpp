/*
 * Thermistor Class
 *
 * A simple object to take care of reading a thermistor
 * and reporting back its value.
 *
 */

#include <Arduino.h>
#include "Thermistor.h"
#include <ctype.h>


unsigned int B;		// Thermisters coefficient of resistance
unsigned long Rd;	// Value of resistor in sereas with thermister
unsigned long R0;	// Value of thermister at ref temperature
float T0;		// Ref temperature
unsigned char Apin;	// The analog pin the thermiste's voltage divider is on
int ATemp[10];		// Keep the ten most recent readings
int ATempIndex = 0;	// Index into average array.
unsigned long timeRef = 0;	// Wait 15 seconds between reading for T ave
int offSet = 0;			// User provide off set temperature.

Thermistor::Thermistor (byte Apin, unsigned long R0, float T0, unsigned long Rd, unsigned int B) {

	this->B = B;
	this->Rd = Rd;
	this->R0 = R0;
	this->T0 = T0;
	this->Apin = Apin;
	this->IdentifierStr = NULL;
}
Thermistor::Thermistor (byte Apin, unsigned long R0, float T0, unsigned long Rd, unsigned int B, char* str) {

	this->B = B;
	this->Rd = Rd;
	this->R0 = R0;
	this->T0 = T0;
	this->Apin = Apin;
	this->IdentifierStr = (char *)str;
}
Thermistor::Thermistor (byte Apin, unsigned long R0, float T0, unsigned long Rd, unsigned int B, int offSet, char* str) {

	this->B = B;
	this->Rd = Rd;
	this->R0 = R0;
	this->T0 = T0;
	this->Apin = Apin;
	this->offSet = offSet;
	this->IdentifierStr = (char *)str;
}

/*
 * 	Calculage the temperature where the thermistor is using Steinhart-Hart
 * 	equation taken from:
 * 	http://en.wikipedia.org/wiki/Thermistor#Steinhart-Hart_equation
 */
int Thermistor::Temperature () {
	unsigned long Rval;	// resistance of thermister
	int ApinVal;	// value on analog pin.  Read once.
	
	ApinVal = analogRead (this->Apin);
	// catch a open or short circuit
	if (ApinVal == 0 || ApinVal == MaxAlalogRead)
		this->Error = true;
	else
		this->Error = false;
	Rval = (MaxAlalogRead - ApinVal) * (this->Rd / ApinVal);
	
	// Calculate the Temperature baised on the resistance of the thermistor. 
	return (this->B/log(Rval/(this->R0*exp(-(this->B/this->T0))))-ZeroCinK+this->offSet);
}

/*
 * 	Need to call this from setup so we can get things ready for
 * 	caculating the average temperature.  Can't do this in the 
 * 	constructor as hardware is not ready.
 */
void Thermistor::aveInit () {
	unsigned char n;

	// Fill ATemp array.  All with current values.  Not the best but 
	// avrage will get better.
	for (n = 0; n < 10; n++) 
		this->ATemp[n] = this->Temperature ();
}


/*
 * 	Build ATemp array with temperature readings.  One every 15 sec.
 */
void Thermistor::buildAve () {

	if ((millis () - this->timeRef) > 15000) {
		this->timeRef = millis ();
		this->ATemp[this->ATempIndex] = this->Temperature ();
		this->ATempIndex = this->ATempIndex + 1;
		if (this->ATempIndex >= 10)
			this->ATempIndex = 0;	
	}
}

/*
 * 	Return the time averaged temperature over ten readings.
 * 	This is used to compansate for iratice thermistor readings.
 */
int Thermistor::AveTemperature () {
	int AveTemp = 0;
	int n;

	for (n=0; n < 10; n++) {
		AveTemp += this->ATemp [n];
	}

	// round 
	if (AveTemp%10 > 4)
		return ((AveTemp/10) + 1);
	else
		return (AveTemp/10);
}

/*
 *	To get half degree temperature accuracy with out using floates
 *	we use the mod10 of the ten member sum.  Then round the 
 *	remainder to 0,5,10 and add it to ten times the interger part of the
 *	average.  So the average temperature is 10 times as big as it really is.
 *	The calling program is responsible for dealing with this fact.
 */
int Thermistor::AveHalfDegree () {
	int AveTemp = 0;
	int n;

	for (n=0; n < 10; n++) {
		AveTemp += this->ATemp [n];
	}

	switch (abs(AveTemp%10)) {
	case 0: case 1: case 2:
		return ((AveTemp/10)*10);
	case 3: case 4: case 5: case 6: case 7:
		return ((AveTemp/10)*10 + 5);
	case 8: case 9:
		return ((AveTemp/10)*10 + 10);
	}
}

/*
 * 	Return the error state of this thermistor.
 */
bool Thermistor::isError () {
	return this->Error;
}

/*
 * 	Set thermistor identifier
 */
void Thermistor::setIdentifierStr (char *str) {
	this->IdentifierStr = (char *)str;
}

/*
 * 	Get thermistor identifier string.
 * 	If the identifier string has not been set it will return null
 * 	which is correct.
 */
char *Thermistor::getIdentifierStr () {
	return this->IdentifierStr;
}
