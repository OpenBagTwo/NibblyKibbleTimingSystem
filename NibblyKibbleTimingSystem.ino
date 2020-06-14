/*
  NibblyKibbleTimingSystem

  Timing system for a Lego race track using three
  IR obstacle sensors--one that detects the presence
  of the starting gate (when gate disappears, race starts)
  and two that function as an IR tripwire to detect when a
  car crosses the finish line.

  The race time is displayed on a 4-digit 7-segment display,
  and all race times for the session can be accessed via
  a web server.

  As most Lego (or Hot Wheels) scale races will look more
  "realistic" when played back in slow motion, this timing
  system supports setting a "Dilation Factor" so that the
  times displayed and reported can match the playback
  duration as opposed to the actual elapsed time.

  A Feather Huzzah was used as the controller for this project,
  but any ESP8266-based controller should work. Also, if you
  don't care about the web server reporting, just comment out
  those sections on pretty much any Arduino-compatible chip.

  The circuit:
  * 3 FC-51 IR obstacle detectors or equivalent
  * 1 TM1637 4-digit 7-segment display (decimal style
    preferred, clock-style will do in a pinch)
  * 1 tactile push button
  * 1 10kΩ resistor

  All components I selected were compatible with either 5V
  or 3.3V logic, so that should give you some freedom as well.

  Created 5 April 2020
  By Gili (OpenBagTwo) Barlev

  http://url/of/online/tutorial.cc

*/

#include <SevenSegmentTM1637.h>  // https://github.com/heatvent/arduino-tm1637
#include <ESP8266WiFi.h>
#include "secrets.h"             // store your Wifi network credentials here

// set system parameters
#define DILATION_FACTOR 2.0  // how long a real-world second should last in timing-system seconds
#define MAX_RACES 256        // max number of races to record

// set GPIO pins
#define START_SENSOR_PIN 14
#define FINISH_SENSOR_PIN_1 12
#define FINISH_SENSOR_PIN_2 13
#define MANUAL_PIN 16
#define PIN_CLK 5
#define PIN_DIO 4

// custom structs
enum state {Staged, Running, Finished};        // the three race states

// global variables
int numRaces;                                  // number of races
float raceTimes[MAX_RACES];                    // times (in dilated seconds) of races

unsigned long startTime;                       // time since bootup (in milliseconds)
unsigned long lastPress;                       // last time the button was pressed (in ms since bootup)

state raceState;                               // state of the race

SevenSegmentTM1637 display(PIN_CLK, PIN_DIO);  // the timing board
WiFiServer server(80);                         // wifi server


void setup() {
  Serial.begin(9600);
  
  numRaces = 0;
  raceState = Staged;
  lastPress = 0;
  
  pinMode(START_SENSOR_PIN, INPUT);
  pinMode(FINISH_SENSOR_PIN_1, INPUT);
  pinMode(FINISH_SENSOR_PIN_2, INPUT);
  pinMode(MANUAL_PIN, INPUT);
  
  display.begin();
  display.setBacklight(100);
  
  Serial.println("Welcome to the Nibbly Kibble Raceway");
  
  connectToWifi();
}


void loop() {
  float raceTime = 0.0;
  
  switch(raceState){
case Staged:
    if (!isStartingGateDown() && !isButtonPressed())
      break;
    startRace();
case Running:
    raceTime = calculateRaceTime();
    if (!isFinishSensorTriggered() && !isButtonPressed())
      break;
    endRace(raceTime);
case Finished:
    raceTime = raceTimes[numRaces - 1];
    
    if (!isStartingGateDown()){
      raceState = Staged;
      display.print("WAIT");
      Serial.println("Resetting");
      delay(1000);
    } else if (isButtonPressed()){
      startRace();
    }
  }

  displayRaceTime(raceTime);
  runWebServer();
  
  delay(5);  // resolution is only 10ms anyway
}


/*
 * Function:  connectToWifi
 * ------------------------
 * Initialize the ESP8266 Wifi server,
 * connect to the local wifi network
 * and display the last octect (so that you
 * know were to find it)
 */
void connectToWifi(){
  Serial.print("Connecting to ");
  Serial.println(ssid);
  display.setDecimalOn(false, 2);
  display.print("WIFI");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Display the IP address
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  display.setDecimalOn(false, 2);
  display.print("IP  ");
  delay(1000);
  display.print(" " + String(ip[3]));
  delay(3000);
}


/*
 * Function:  calculateRaceTime
 * ----------------------------
 * Calculate the duration of the current race (in "race time" as opposed to real-world
 * time) based on the number of milliseconds elapsed since the race began
 * 
 * returns: the elapsed time of the current race, in seconds of "race time"
 */
float calculateRaceTime(){
  // calculate number of dilated seconds since start of race
  return (((float) millis()) - startTime) / 1000 * DILATION_FACTOR;
}


/*
 * Function:  isStartingGateDown
 * -----------------------------
 * Determine whether the starting gate is down (and thus the race has started).
 * According to the logic of the FC-51, the output of the sensor pin will be LOW if
 * an obstacle (in this case, the starting gate) is detected.
 * 
 * returns: true if the starting gate is down, false otherwise
 */
bool isStartingGateDown(){
  return (digitalRead(START_SENSOR_PIN) == HIGH);
}


/*
 * Function:  isFinishSensorTriggered
 * ----------------------------------
 * Determine whether an object has crossed the finish line (and thus the race is over).
 * The finish line for the Nibbly Kibble is two FC-51 IR sensors aimed at each other. Thus,
 * instead of looking for an obstacle to reflect the IR signal, we're waiting for the IR
 * beam to be interrupted. Based on the ranges and tuning of the IR sensors used in my setup,
 * it was frequently the case that only one sensor would go HIGH when a car crossed the finish line,
 * as the car would be close enough to the other sensor to reflect its own IR signal.
 * 
 * returns: true if an object is detected crossing the finish, false otherwise
 */
bool isFinishSensorTriggered(){
  return ((digitalRead(FINISH_SENSOR_PIN_1) == HIGH) ||  (digitalRead(FINISH_SENSOR_PIN_2) == HIGH));
}


/*
 * Function:  isButtonPressed
 * --------------------------
 * Determine whether the manual tactile button has been pressed, with
 * logic in place to prevent "double clicks" or "long presses" from being
 * registered. This button in my circuit was wired using a pull-up resistor,
 * so the pin would go LOW when the button was pressed, but I could easily
 * have used a pull-down configuration. 
 * 
 * returns: true if the button is pushed, false otherwise
 */
bool isButtonPressed(){
  unsigned int currentTime = millis();
  if (currentTime - lastPress < 200)
    return 0; // prevent double-press
  if (digitalRead(MANUAL_PIN) == LOW){
    Serial.println("Button pressed");
    lastPress = currentTime;
    return 1;
  }
  return 0;
}


/*
 * Function:  displayRaceTime
 * --------------------------
 * Display the elapsed time of the current (or last completed) race on
 * the 4-digit 7-segment display.
 * 
 * raceTime: the elapsed time of the current (or last completed) race
 *           in seconds of "race time"
 */
void displayRaceTime(float raceTime){
  //display the race time on the timing board
  String message = String(((int) (raceTime * 100)));

  if (raceTime > 100){
    /*
     * I could turn off the colon and just start counting seconds
     * but there's no way a race on my track would ever be more than
     * 100 seconds so the goal here is just to not freak out the display
     */
    display.setDecimalOn(false, 2);
    display.print("OFLW");
    return;
  }

  while (message.length() < 4)
    message = "0" + message;

  display.setDecimalOn(true, 2);
  display.print(message);
}


/*
 * Function:  startRace
 * --------------------
 * Begin the race
 */
void startRace(){
  // begin the race
  startTime = millis();
  Serial.println("Begin Race " + String(numRaces + 1));
  raceState = Running;
}


/*
 * Function:  endRace
 * ------------------
 * End the race
 */
void endRace(float raceTime){
  // end the race and log the race time
  raceTimes[numRaces] = raceTime;
  displayRaceTime(raceTime);
  numRaces++;

  Serial.println("Race " + String(numRaces) +
                 " Time: " + String(raceTime));

  if (numRaces == MAX_RACES)
    numRaces = 0;  // prevent overflow
  
  raceState = Finished;
  for (int i=0; i<4; i++){
    display.setBacklight(0);
    delay(500 / DILATION_FACTOR);
    display.setBacklight(100);
    delay(1000 / DILATION_FACTOR);
  }
}


/*
 * Function:  runWebServer
 * -----------------------
 * Serve the times of every race in the current session on the
 * ESP8266's web server. This code is adapted from the WifiWebServer
 * example sketch from https://github.com/esp8266/Arduino
 */
void runWebServer(){
  // Check if a client has connected
  WiFiClient client = server.available();

  if (!client)
    return;

  // Wait until the client sends some data
  Serial.println("-----------------------");
  Serial.println("new client");
  while (!client.available()) {
    delay(1);
  }
  
  // Read the first line of the request
  String clientRequest = client.readStringUntil('\r');
  Serial.println(clientRequest);
    
  client.flush();
  
  // Prepare the response
  String esp8266Response = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n\r\n";

  esp8266Response += "Nibbly Kibble Raceway: <br>\r\n";

  for (int i = 0; i < numRaces; i++)
    esp8266Response += "Race " + String(i + 1) + ": " + String(raceTimes[i]) + "s<br>\r\n";
  
  // Send the response to the client
  client.print(esp8266Response);
  delay(1);
  Serial.println("Client disonnected");
  // The client will actually be disconnected
  // when the function returns and 'client' object is detroyed
}
