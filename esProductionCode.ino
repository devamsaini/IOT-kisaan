#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ThingSpeak.h"

//pin definitions ---------------------------------------------------------------------------

const int relayPin1 = 14;     // Relaypin1 connected to D5 air 
const int relayPin2 = 12;     // Relaypin2 connected to D6 water
const int relayPin3 = 16;     // Relaypin3 connected to D0 sol A
const int relayPin4 = 13;     // Relaypin4 connected to D7 sol B
const int ledPin = 2;         // LED connected to D2
namespace pin {
  const byte tds_sensor = A0;
  const byte one_wire_bus = D3; // Dallas Temperature Sensor
}

//tds threshold values for dosing control-----------------------------------------------------

const int tdslimit = 900;
const long dosinginterval = 3 * 60 * 1000;  // dosing interval time
int doseflag = 0;                           // 0 for sol A; 1 for sol B; 
int soldelay = 5 * 1000;                    // dosing pump on time interval 


//periodic time definitions -----------------------------------------------------------------

const long rst = 6 * 60 * 60 * 1000;    // system reset time
const long interval1 = 7 * 60 * 1000;   // overall repeat time

const long duration1 = 2 * 60;          // air pump run time (in seconds)
const long duration2 = 5 ;              // water pump run time (in seconds)

const long ledBlinkInterval = 2000;     // led idle state delay
const long ledBurstDuration = 75;       // led burst state delay

const long thingsInterval = 7 * 1000;  // thingspeak update time (during idle state)
const long thingsdelay = 15;             // thingspeak update time (during working delay loops) (in seconds)

unsigned long previousMillis1 = 0;      //time variable for main overall repeat
unsigned long previousMillisLED = 0;    //time variable for led 
unsigned long previousMillisthings = 0; //time variable for thingspeak server data upload
unsigned long previousMillisdosing = 0; //time variable for dosing pumps

int flag = 1;                           // set-up flag for first time trial 
const long setuplooptime = 1 * 60;      // set-up loop wait time  (in seconds)

//display setings ----------------------------------------------------------------------------------

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//wifi and thingspeak stuff  -----------------------------------------------------------------------

char ssid[] = "Hello moto";
char pass[] = "12345678";

String apiKey = "JEGYEPSC4ZM6190W";     //  Enter your Write API key from ThingSpeak.
const char* server = "api.thingspeak.com";
unsigned long channel =2474084;         //  Enter the channel number to control the relays.
WiFiClient client;

//sensors setup  -------------------------------------------------------------------------------------

namespace device {
  float aref = 3.3; // Vref, this is for 3.3v compatible controller boards, for Arduino use 5.0v.
}
namespace sensor {
  float ec = 0;
  unsigned int tds = 0;
  float waterTemp = 0;
  float ecCalibration = 1;
}
OneWire oneWire(pin::one_wire_bus);
DallasTemperature dallasTemperature(&oneWire);


//function to blink the led ***********************************************************************************
void ledblnk(){
  digitalWrite(ledPin, LOW);
  delay(ledBurstDuration);
  digitalWrite(ledPin, HIGH);
  delay(ledBurstDuration);
}

// function to process the sensor data **************************************************************************
void readTdsQuick() {

  //read the sensors-------------------------------------------------------------------------------------------

  dallasTemperature.requestTemperatures();
  sensor::waterTemp = dallasTemperature.getTempCByIndex(0);
  float rawEc = analogRead(pin::tds_sensor) * device::aref / 1024.0;
  float temperatureCoefficient = 1.0 + 0.02 * (sensor::waterTemp - 25.0);
  sensor::ec = (rawEc / temperatureCoefficient) * sensor::ecCalibration;
  sensor::tds = (133.42 * pow(sensor::ec, 3) - 255.86 * sensor::ec * sensor::ec + 857.39 * sensor::ec) * 0.5;
  Serial.print(F("TDS:")); Serial.println(sensor::tds);
  Serial.print(F("EC:")); Serial.println(sensor::ec, 2);
  Serial.print(F("Temperature:")); Serial.println(sensor::waterTemp, 2);

  //print sensor data onto the oled display -------------------------------------------------------------------

  display.clearDisplay();
  display.setCursor(10, 0);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print("TDS:" + String(sensor::tds));
  display.setCursor(10, 20);
  display.setTextSize(2);
  display.print("EC:" + String(sensor::ec, 2));
  display.setCursor(10, 45);
  display.setTextSize(2);
  display.print("T:" + String(sensor::waterTemp, 2));
  display.display();
}

//fucntion to send data to thingspeak  ************************************************************************
void thingsend(){
  Serial.println("sending data");
  if (client.connect(server,80))   //   "184.106.153.149" or api.thingspeak.com
  {        
    String postStr = apiKey;
    postStr +="&field1=";
    postStr += String(sensor::waterTemp);
    postStr +="&field2=";
    postStr += String(sensor::ec);
    postStr +="&field3=";
    postStr += String(sensor::tds);
    postStr += "\r\n\r\n\r\n";
    Serial.println("%. Sending to Thingspeak.");
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: "+apiKey+"\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    Serial.println("sent");
  }
  client.stop();
}

//function to create a delay while still maintaining other functionality ************************************
void workdelay(long seconds){
  for(int i=0; i<seconds/2; i++){
    ledblnk();
    delay(150);
    ledblnk();
    delay(150);
    ledblnk();
    delay(150);
    readTdsQuick(); // creates ~500 delay
    if(i%thingsdelay==0){
      thingsend();
    }
    else{
      delay(600);
    }
  }
}

//function to perform the required dosing action

int dosing(int flag){
  digitalWrite(ledPin, LOW);
  if (flag==0){
    digitalWrite(relayPin3, LOW);     // Activate sol A pump
    delay(soldelay);
    digitalWrite(relayPin3, HIGH);    // Deactivate sol A pump
    digitalWrite(ledPin, HIGH);
    return 1;
  }
  if (flag==1){
    digitalWrite(relayPin4, LOW);     // Activate sol b pump
    delay(soldelay);
    digitalWrite(relayPin4, HIGH);    // Deactivate sol b pump
    digitalWrite(ledPin, HIGH);
    return 0;
  }
  return 0;
}

//function to run the air and water pumps *******************************************************************
void airwater(){ // eats up 3 mins
  digitalWrite(relayPin1, LOW);     // Activate air pump
  workdelay(duration1);
  digitalWrite(relayPin1, HIGH);    // Deactivate air pump
  workdelay(2);
  for(int i=0; i<3; i++){           //cycle on and off to maintain low flow rate
    digitalWrite(relayPin2, LOW);   // Activate water pump
    workdelay(duration2);  
    digitalWrite(relayPin2, HIGH);  //deactivate water pump
    workdelay(duration2*3);
  }
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%   Main code   %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void setup() {

  //setup the pins and set to off -----------------------------------------------------------------------------------
  
  Serial.begin(9600); 
  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  pinMode(relayPin3, OUTPUT);
  pinMode(relayPin4, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  digitalWrite(relayPin1, HIGH);
  digitalWrite(relayPin2, HIGH);
  digitalWrite(relayPin3, HIGH);
  digitalWrite(relayPin4, HIGH);

  //connect to the wifi -----------------------------------------------------------------------------------------------

  Serial.println("Connecting to ");
  Serial.print(ssid); 
  WiFi.begin(ssid, pass); 
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  //setup display, sensors and thingspeak -------------------------------------------------------------------------------
  
  dallasTemperature.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  delay(1000);
  display.clearDisplay();
  display.setTextColor(WHITE);
  ThingSpeak.begin(client);

}

void loop() {
  unsigned long currentMillis = millis();

  //initial trial loop  --------------------------------------------------------------------------------------------
  if (flag==1){
    for(int i=0;i<setuplooptime/2; i++){
      ledblnk();
      delay(300);
      ledblnk();
      delay(300);
      readTdsQuick();  //creates ~500 delay
      if(i%thingsdelay==0){
        thingsend();
      }
      else{
        delay(600);
      }
    }
    airwater();
    flag=0;
  }

  // routine system restart  ----------------------------------------------------------------------------------------
  if (currentMillis == rst){
    ESP.restart();
  }

  // Blink LED two short bursts for idle state -----------------------------------------------------------------------
  if (currentMillis - previousMillisLED >= ledBlinkInterval) {
    previousMillisLED = currentMillis;
    ledblnk();
    ledblnk();
    readTdsQuick();
  }

  // send sensor data to thingspeak when idle ------------------------------------------------------------------------
  if (currentMillis - previousMillisthings >= thingsInterval) {
    previousMillisthings = currentMillis;
    thingsend();
  }

  // check tds and run dosing pump accordingly  ---------------------------------------------------------------------
  if (currentMillis - previousMillisdosing >= dosinginterval) {
    previousMillisdosing = currentMillis;
    if (doseflag==1||sensor::tds<tdslimit){
      doseflag=dosing(doseflag);
    }
  }

  // overall repeat loop  -------------------------------------------------------------------------------------------
  if (currentMillis - previousMillis1 >= interval1) {
    previousMillis1 = currentMillis;
    airwater();
  }

}
