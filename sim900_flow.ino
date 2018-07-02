/**/

#include <SoftwareSerial.h>
#include <EEPROM.h>

// ID of the settings block
#define CONFIG_VERSION "ls2"

// Tell it where to store your config data in EEPROM
#define CONFIG_START 0

#define DATASTART 100

#define MAXSTORECNT 5

// Example settings structure
struct StoreStruct {
	// This is for mere detection if they are your settings
	char version[4];
	// The variables of your settings
	uint8_t storedCount;
} storage = {
	CONFIG_VERSION,
	// The default values
	0
};

typedef struct
{
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
} MyDateTime;

typedef struct
{
	MyDateTime startTime;
	MyDateTime endTime;
	double flow;
} DataRow;

void saveDataRow(DataRow newRow);
String myTimeToString(MyDateTime dateTime);

const char destNumber[] = "+393477901668";
// Configure software serial port
SoftwareSerial SIM900(7, 8);
String incomingSMSStr = "";

const uint32_t PULSECNTPERLITRE = 492;
static uint32_t lastPulseTime = 0;
const uint32_t NONEFLOWINTVAL = 1000;
const uint8_t SENSORPIN = 2;
volatile uint32_t flowPulseCnt; //measuring the rising edges of the signal from flow sensor
volatile bool interrupt = false;
bool existFlow = false;

void flowISR()     //This is the function that the interupt calls
{
	flowPulseCnt++;
	interrupt = true;
}

uint8_t year, month, day, hour, min, sec;
DataRow tempRow;
void setup()
{
	// load configuration from eeprom
	loadConfig();

	// arduino communicates with SIM900 GSM shield at a baud rate of 19200
	// Make sure that corresponds to the baud rate of your module
	SIM900.begin(19200);
	Serial.begin(115200);

	Serial.println(F("program started"));
	// give time to your GSM shield log on to network
	delay(20000);

	// AT command to set SIM900 to SMS mode
	SIM900.print("AT+CMGF=1\r");
	printSerialData(5000);

	// Set module to send SMS data to serial out upon receipt 
	SIM900.print("AT+CNMI=2,2,0,0,0\r");
	printSerialData(5000);

	// interrupt for sensor
	lastPulseTime = millis();
	pinMode(SENSORPIN, INPUT);
	attachInterrupt(0, flowISR, RISING); //
	Serial.println(F("setup completed"));
	printSerialData(5000);

	// getTimeStamp(year, month, day, hour, min, sec);
	getTimeStamp(year, month, day, hour, min, sec);

	setTimeStamp(18, 6, 20, 15, 19, 0);
	// sendDataItems();
}

void loop()
{
	static uint32_t lastSim900Time = millis();
	// check sms in sim900 shield
	while (SIM900.available() > 0)
	{
		// Get the character from the cellular serial port
		char ch = (char)SIM900.read();
		lastSim900Time = millis();
		incomingSMSStr += String(ch);
	}

	if (millis() - lastSim900Time > 500 && incomingSMSStr.length())
	{
		lastSim900Time = millis();
		Serial.print(F("received message:"));
		Serial.println(incomingSMSStr);
		incomingSMSStr = "";
		Serial.println();

		// Serial.println(F("sending data as http get"));
		sendDataItems();
	}

	if (interrupt)
	{
		interrupt = false;
		lastPulseTime = millis();

		if (!existFlow)
		{
			existFlow = true;
			Serial.println(F("flow has started"));
			getTimeStamp(year, month, day, hour, min, sec);

			// get start date time
			tempRow.startTime.year = year;
			tempRow.startTime.month = month;
			tempRow.startTime.day = day;
			tempRow.startTime.hour = hour;
			tempRow.startTime.min = min;
			tempRow.startTime.sec = sec;

			Serial.print(F("starting time is "));
			Serial.println(myTimeToString(tempRow.startTime));
		}
	}

	if (millis() - lastPulseTime > NONEFLOWINTVAL && existFlow)
	{
		existFlow = false;
		lastPulseTime = millis();
		Serial.println(F("flow finished in sensor"));
		getTimeStamp(year, month, day, hour, min, sec);
		// get end date time
		tempRow.endTime.year = year;
		tempRow.endTime.month = month;
		tempRow.endTime.day = day;
		tempRow.endTime.hour = hour;
		tempRow.endTime.min = min;
		tempRow.endTime.sec = sec;

		// calculation liter from pulse count
		tempRow.flow = (double)flowPulseCnt / (double)PULSECNTPERLITRE;
		flowPulseCnt = 0;

		Serial.println(F("new data will be saved into EEPROM"));
		Serial.print(F("starting time is "));
		Serial.println(myTimeToString(tempRow.startTime));
		Serial.print(F("end time is "));
		Serial.println(myTimeToString(tempRow.endTime));
		Serial.print(F("flow is "));
		Serial.print(tempRow.flow, 2);
		Serial.println(F("L."));
		saveDataRow(tempRow);
	}//*/

}

void sendDataItems()
{
	Serial.print("stored row count is ");
	Serial.println(storage.storedCount);
	String response = "";
	DataRow temp;
	for (int8_t idx = 0; idx < storage.storedCount; idx++)
	{
		// read data
		uint32_t readAddr = DATASTART + idx * sizeof(DataRow);
		for (uint32_t t = 0; t<sizeof(DataRow); t++)
			*((char*)&temp + t) = EEPROM.read(readAddr + t);

		String startTime = myTimeToString(temp.startTime);
		String endTime = myTimeToString(temp.endTime);

		// sending http get request part
		// httpGetRequest(startTime, endTime, temp.flow);
		sendSMS(startTime, endTime, temp.flow);
	}
}

char timeStamp[300];
bool getTimeStamp(uint8_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &min, uint8_t &sec)
{
	SIM900.println("AT+CCLK?");      //SIM900 AT command to get time stamp
	uint32_t lastTime = millis();
	bool bReceiving = false;
	Serial.println(F("get timestamp started"));
	while (true)
	{
		if (millis() - lastTime > 5000)
		{
			Serial.println(F("timeout happend"));
			return false;
		}
		else if (SIM900.available() > 0)
		{
			bReceiving = true;
			lastTime = millis();
			break;
		}
	}

	int i = 0;
	while (millis() - lastTime < 100)
	{
		if (SIM900.available() > 0) {
			char ch = (char)SIM900.read();
			timeStamp[i] = ch;
			// Serial.print(ch);
			// Serial.print(",");
			// Serial.println(i);
			i++;
			lastTime = millis();
		}
	}

	if (i > 50)
	{
		Serial.println(F("invalid timestamp response"));
		return false;
	}


	year = (((timeStamp[26]) - 48) * 10) + ((timeStamp[27]) - 48);
	month = (((timeStamp[23]) - 48) * 10) + ((timeStamp[24]) - 48);
	day = (((timeStamp[20]) - 48) * 10) + ((timeStamp[21]) - 48);
	hour = (((timeStamp[29]) - 48) * 10) + ((timeStamp[30]) - 48);
	min = (((timeStamp[32]) - 48) * 10) + ((timeStamp[33]) - 48);
	sec = (((timeStamp[35]) - 48) * 10) + ((timeStamp[36]) - 48);

	return true;
}

bool setTimeStamp(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec)
{
	String timeStamp = "AT+CCLK=\"";
	if (year < 10)
		timeStamp += "0";
	timeStamp += String(year);
	timeStamp += "/";
	if (month < 10)
		timeStamp += "0";
	timeStamp += String(month);
	timeStamp += "/";
	if (day < 10)
		timeStamp += "0";
	timeStamp += String(day);
	timeStamp += ",";

	if (hour < 10)
		timeStamp += "0";
	timeStamp += String(hour);
	timeStamp += ":";
	if (min < 10)
		timeStamp += "0";
	timeStamp += String(min);
	timeStamp += ":";
	if (sec < 10)
		timeStamp += "0";
	timeStamp += String(sec);
	timeStamp += "+08";
	timeStamp += "\"";

	SIM900.println(timeStamp);      //SIM900 AT command to set time stamp

	uint32_t lastTime = millis();
	bool bReceiving = false;
	while (true)
	{
		if (millis() - lastTime > 5000)
		{
			Serial.println(F("timeout happend"));
			return false;
		}
		else if (SIM900.available() > 0)
		{
			bReceiving = true;
			lastTime = millis();
			break;
		}
	}

	int i = 0;
	while (millis() - lastTime < 100)
	{
		if (SIM900.available() > 0) {
			char ch = (char)SIM900.read();
			Serial.print(ch);
			lastTime = millis();
		}
	}
	return true;
}

void loadConfig() {
	// To make sure there are settings, and they are YOURS!
	// If nothing is found it will use the default settings.
	if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
		EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
		EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
		for (unsigned int t = 0; t<sizeof(storage); t++)
			*((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
}

void saveConfig() {
	for (unsigned int t = 0; t<sizeof(storage); t++)
		EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
}

String readDataRows()
{
	String response = "";
	DataRow temp;
	for (int8_t idx = 0; idx < storage.storedCount; idx++)
	{
		// read data
		uint32_t readAddr = DATASTART + idx * sizeof(DataRow);
		for (uint32_t t = 0; t<sizeof(DataRow); t++)
			*((char*)&temp + t) = EEPROM.read(readAddr + t);

		response += F("start-");
		response += myTimeToString(tempRow.startTime);

		response += F(", end-");
		response += myTimeToString(tempRow.endTime);

		response += F(", flow-");
		response += String(tempRow.flow, 2);
		response += F("L.");
		response += F("\r\n");
	}

	return response;
}

void saveDataRow(DataRow newRow)
{
	DataRow temp;

	for (int8_t idx = storage.storedCount; idx > 0; idx--)
	{
		// read original data
		uint32_t readAddr = DATASTART + (idx - 1) * sizeof(DataRow);
		for (uint32_t t = 0; t<sizeof(DataRow); t++)
			*((char*)&temp + t) = EEPROM.read(readAddr + t);

		// write original data
		uint32_t writeAddr = DATASTART + idx * sizeof(DataRow);
		for (unsigned int t = 0; t<sizeof(DataRow); t++)
			EEPROM.write(writeAddr + t, *((char*)&temp + t));
	}

	if (storage.storedCount < MAXSTORECNT)
		storage.storedCount++;

	// write new data
	uint32_t writeAddr = DATASTART;
	for (unsigned int t = 0; t<sizeof(DataRow); t++)
		EEPROM.write(writeAddr + t, *((char*)&newRow + t));

	saveConfig();
}

void printSerialData(uint32_t timeout)
{
	uint32_t lastMs = millis();
	while (true)
	{
		if (SIM900.available())
		{
			lastMs = millis();
			break;
		}
		else if (millis() - lastMs > timeout)
			return;
	}

	while (lastMs - millis() < 400)
	{
		if (SIM900.available())
		{
			char ch = SIM900.read();
			Serial.print(ch);
			lastMs = millis();
		}
	}

	Serial.println("");
}


void httpGetRequest(String startDate, String endDate, double liters)
{
	SIM900.println("AT+CGATT=1");
	printSerialData(200);

	SIM900.println("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");//setting the SAPBR,connection type is GPRS
	printSerialData(1000);

	SIM900.println("AT+SAPBR=3,1,\"m2m.vodafone.it\",\"\"");//setting the APN,2nd parameter empty works for all networks
	printSerialData(5000);

	SIM900.println();
	SIM900.println("AT+SAPBR=1,1");
	printSerialData(10000);

	SIM900.println("AT+HTTPINIT"); //init the HTTP request
	printSerialData(2000);

	String requestURL = "AT+HTTPPARA=\"URL\",\"http://www.fakeurl.cloud/postpage.php?datestart=";
	requestURL += startDate;
	requestURL += "&dateend=";
	requestURL += endDate;
	requestURL += "&liters=";
	requestURL += String(liters, 2);
	requestURL += F("\"");

	SIM900.println(requestURL);// setting the httppara,
	printSerialData(1000);
	SIM900.println();
	SIM900.println("AT+HTTPACTION=0");//submit the GET request
	printSerialData(8000);

	SIM900.println("AT+HTTPREAD=0,20");// read the data from the website you access
	printSerialData(3000);

	SIM900.println("");
	SIM900.println("AT+HTTPTERM");// terminate HTTP service
	printSerialData(1000);
}

String myTimeToString(MyDateTime dateTime)
{
	String result = "";
	if (dateTime.year < 10)
		result += "0";
	result += String(dateTime.year);
	result += "/";
	if (dateTime.month < 10)
		result += "0";
	result += String(dateTime.month);
	result += "/";
	if (dateTime.day < 10)
		result += "0";
	result += String(dateTime.day);
	result += ",";

	if (dateTime.hour < 10)
		result += "0";
	result += String(dateTime.hour);
	result += ":";
	if (dateTime.min < 10)
		result += "0";
	result += String(dateTime.min);
	result += ":";
	if (dateTime.sec < 10)
		result += "0";
	result += String(dateTime.sec);
	return result;
}

void sendSMS(String startDate, String endDate, double liters)
{
	String strMessage;
	strMessage = "datestart-";
	strMessage += startDate;
	strMessage += ", dateend-";
	strMessage += endDate;
	strMessage += ", ";
	strMessage += String(liters, 2);
	strMessage += "Liters";

	Serial.println(strMessage);

	// AT command to set SIM900 to SMS mode
	SIM900.print("AT+CMGF=1\r");
	delay(100);

	String atCommand = "AT+CMGS=\"" + String(destNumber);
	atCommand += "\"";
	SIM900.println(atCommand);
	delay(100);

	SIM900.println(strMessage);
	delay(100);

	// End AT command with a ^Z, ASCII code 26
	SIM900.println((char)26);
	delay(100);
	SIM900.println();
	// Give module time to send SMS
	delay(8000);
	printSerialData(2000);
}