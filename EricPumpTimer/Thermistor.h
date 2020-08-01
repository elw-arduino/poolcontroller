#ifndef Thermistor_h
#define Thermistor_h

#define ZeroCinK 273.15
#define MaxAlalogRead 1023

class Thermistor {
	public:
		// Constructor
		Thermistor (
			unsigned char pin, // analog pin of thermistor
			unsigned long R0, // value of thermistor at T0
			float T0,	// reference temperature of thermistor
			unsigned long Rd, // value of resistor dividor
			unsigned int B); // Temparute coeficent of thermistor
		Thermistor (
			unsigned char pin, // analog pin of thermistor
			unsigned long R0, // value of thermistor at T0
			float T0,	// reference temperature of thermistor
			unsigned long Rd, // value of resistor dividor
			unsigned int B, // Temparute coeficent of thermistor
			char *str);	// identifier string
		Thermistor (
			unsigned char pin, // analog pin of thermistor
			unsigned long R0, // value of thermistor at T0
			float T0,	// reference temperature of thermistor
			unsigned long Rd, // value of resistor dividor
			unsigned int B, // Temparute coeficent of thermistor
			int offSet,	// off set temperature.
			char *str);	// identifier string

		// class functions
		int Temperature ();	// return the thermistor's temperature
		void aveInit ();	// setup for temp ave caculation
		void buildAve ();	// takes a temperature reading / sec
		int AveTemperature ();	// return the average temperature reading
		int AveHalfDegree ();	// return the int part of average temperature
		bool isError ();	// return true if we have an error
		char *getIdentifierStr ();// return identifier string
		void setIdentifierStr (char *str);// set identifier string
		char *IdentifierStr;
		int offSet;

	private:
		unsigned int B;	// Thermisters coefficient of resistance
		unsigned long Rd;// Value of resistor in sereas with thermister
		unsigned long R0;// Value of thermister at ref temperature
		float T0;// Ref temperature
		unsigned char Apin;// The analog pin the thermistor's voltage divider is on
		int ATemp[10];//hold temperature values to be averaged
		int ATempIndex;//index into ATemp, a ring buffer
		unsigned long timeRef;// Wait 15 seconds between reading for T ave
		bool Error;//true if we have an error.
};

#endif
