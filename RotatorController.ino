/*
Rotator Controller

Board: LILYGO T-Display S3 

!!!!****** Set correct display type in .../libraries/TFT_eSPI/User_Setup_Select.h
LILYGO T-Display S3 ....... Setup24_ST7789.h

!!!!****** Needs modified (by Liligo) TFT_eSPI library

Hardware Selection in Tools Menu:
Board: ESP32S3 Dev Module (ESP32-S3-DevKitC-1)
Upload Speed: 921600
USB Mode: "Hardware CDC and JTAG"
USB CDC On Boot: "Enabled"
USB Firmware MSC On Boot: "Disabled"
USB DFU On Boot: Disabled
Upload Mode: "UART0 / Hardware CDC"
CPU Frequency: "240MHz (WiFi)"
Flash Mode: "QIO 80MHz"
Flash Size: "16MB (128Mb)"
Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
Core Debug Level: "None"
PSRAM: "OPI PSRAM"
Arduino Runs On: "Core 0"
Events Run On: "Core 1"

*/

/**
 * Idiom Press Rotor-EZ and RotorCard interfaces
 * Support for AZ only
 *
 * Set Target Position: "AP1xxx;" where xxx = 000 to 360
 * Start rotation to target: "AM1;"
 * Set Target and start rotation "AP1xxx<CR>"
 * Get current bearing: "AI1;"
 * Stop movement: ";"
 * Report Version: "V"
 *
 */

#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "time.h"
#include "TFT_eSPI.h"
#include "img_logo.h"

const char* host = "esp32";
const char* ssid = "CT";
const char* password = "Siemens300Omron";

const char* versionStr = "VK3ERW rotator";

int actualBearing = 123;
int targetBearing = 189;

bool moveRight = false;
bool moveLeft = false;

String commandStr = "";

#define ANALOG_PIN 44
int analogCount;
#define ANALOG_MAX_MV 3000  // Voltage at full scale of analog input
int analogMilliVolts;

// Time
long nextTimeUpdate = 0;
#define TIME_UPDATE_INTERVAL 1000
const char* ntpServer = "au.pool.ntp.org";
#define TZ_STRING "AEST-10AEDT,M10.1.0,M4.1.0/3"    // Australia/Melbourne
const long  gmtOffset_sec = 36000;
const int   daylightOffset_sec = 0;
char timeHour[3]="00";
char timeMin[3]="00";
char timeSec[3];

char m[12];
char y[5];
char d[3];
char dw[12];


// TFT display definitions
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
#define GREY 0x6B6D
#define BLUE 0x0967
#define ORANGE 0xC260
#define PURPLE 0x604D
#define GREEN 0x1AE9
#define TFT_X_MAX 320
#define TFT_Y_MAX 170
#define TFT_ROTATION 1
int gw=204;
int gh=102;
int gx=110;
int gy=144;
int curent=0;


// Control Task definitions
TaskHandle_t controlTaskHandle;
xTimerHandle controlTimer;
// xtask params
#define CTRL_TMR_START_WAIT 1000   // wait if non-responsive
#define CTRL_TMR_DUR  20     // mS between callbacks. Should be >= 1mS to allow other processing
#define CTRL_TASK_STACK    10000  // Stack size for control task
#define CTRL_TASK_CPU  1
#define CTRL_TASK_PRI  2   // higher number than loop() == 1
volatile long controlTime = 0;
long controlStart;

void timeInit() {
  //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  configTime(0, 0, ntpServer);
  setenv("TZ",TZ_STRING,1);  //  adjust the TZ
  tzset();
  getLocalTime();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Rotor Controller");

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection establishment
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  timeInit();
  
  // TFT display 
  tft.begin();
  tft.setRotation(TFT_ROTATION);
  tft.pushImage(0, 0, TFT_X_MAX, TFT_Y_MAX, (uint16_t *)img_logo);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
  sprite.createSprite(320, 170);
  sprite.setTextDatum(3);
  sprite.setSwapBytes(true);
  drawScreen();
  
  // xTask for control()
  xTaskCreatePinnedToCore(controlTask, "cTask", CTRL_TASK_STACK, NULL, CTRL_TASK_PRI, &controlTaskHandle, CTRL_TASK_CPU);
  controlTimer = xTimerCreate("timerC", pdMS_TO_TICKS(CTRL_TMR_DUR), true, (void*)0, controlCallback);
  // controlTask suspends after execution, the follwing timer will resume the control task
  xTimerStart(controlTimer, CTRL_TMR_START_WAIT);
  controlStart = micros();
  Serial.printf("Started controlTask on CPU core %d with priority %d\n", CTRL_TASK_CPU, CTRL_TASK_PRI );

}

void drawScreen() {
  sprite.fillSprite(TFT_BLACK);
  // sprite.setTextDatum(4);
  sprite.setTextColor(TFT_WHITE, BLUE);
  sprite.fillRoundRect(6,5,38,32,4, BLUE);
  sprite.fillRoundRect(52,5,38,32,4, BLUE);
  sprite.fillRoundRect(6,42,80,12,4, BLUE);
  sprite.fillRoundRect(6,82,78,76,4, PURPLE);
  sprite.fillRoundRect(6,58,80,18,4, GREEN);
  sprite.drawString(String(timeHour),10,24,4);
  sprite.drawString(String(timeMin),56,24,4);
  sprite.drawString(String(m)+" "+String(d),10,48);
  sprite.drawString(String(timeSec),gx-14,14,2);
  sprite.setTextColor(TFT_WHITE,PURPLE);
  sprite.drawString("Raw: "+ String(analogCount),10,92,2);
  sprite.drawString("mV: " + String(analogMilliVolts),10,108,2);
  sprite.drawString("MAX:",10,138,2);
  //sprite.drawString(String(dw)+", "+String(y),10,58);    
  //sprite.drawString(String(analogRead(18)),gx+160,26);

  sprite.setTextColor(TFT_WHITE, GREEN);
  sprite.drawString("SPEED:",10,68);
  sprite.setTextColor(TFT_YELLOW,TFT_BLACK);
  sprite.drawString("ANALOG READINGS",gx+10,16,2);
  sprite.drawString("ON PIN 12",gx+10,30);
  sprite.setFreeFont();

  // Vertical Lines
  for(int i=1;i<12;i++) {
    sprite.drawLine(gx+(i*17),gy,gx+(i*17),gy-gh,GREY);
    if(i*17%34==0)
      if(i*2<10)
        sprite.drawString("0"+String(i*2),gx+(i*17)-3,gy+8);
      else
        sprite.drawString(String(i*2),gx+(i*17)-4,gy+8);
  }

  // Horizontal Lines
  for(int i=1;i<6;i++){
    sprite.drawLine(gx,gy-(i*17),gx+gw,gy-(i*17),GREY);
    sprite.drawString(String(i*17),gx-16,gy-(i*17));
  }

  // Thick lines
  sprite.drawLine(gx,gy,gx+gw,gy,TFT_WHITE);
  sprite.drawLine(gx,gy,gx,gy-gh,TFT_WHITE);
  
  sprite.pushSprite(0,0);
}

/*
 * Convert Bearing to Rotator Position 
 * Rotor Pos 359 = Brg 180
 * Rotor Pos 180 = Brg 0
 * Rotor Pos 0 = Brg 181
 */
int bearingToPos(int bearing) {
  if (bearing < 180) {
    return (bearing + 180);
  } else {
    return (bearing - 180);
  }
}

void moveStart() {
  Serial.println("Start move");
  int targetPos = bearingToPos(targetBearing);
  Serial.print("Moving to rotPos: ");
  Serial.println(targetPos);
  int actualPos = bearingToPos(actualBearing);
  if (actualPos < targetPos) {
    moveRight = true;
    Serial.println("Dir: Right");
  } else {
    moveLeft = true;
    Serial.println("Dir: Left");
  }

}

void moveStop() {
  Serial.println("Stop move");
}

bool cmdPosition() {
  if ((commandStr.length() < 4)) return false;

  // Set position
  if ((commandStr.charAt(1) == 'P')) {
    if ((commandStr.length() < 7)) return false;
    String posStr = commandStr.substring(3,6);
    targetBearing = posStr.toInt();
    Serial.print("New Target: ");
    Serial.println(targetBearing);
    // CR at the end will start  move
    if((commandStr.charAt(6) == 13 )) {
      moveStart();
    }        
  }

  if ((commandStr.charAt(1) == 'M')) {
    moveStart();
  }

  if ((commandStr.charAt(1) == 'I')) {
    char sendStr[8] = "";
    sprintf(sendStr, ";%03d", actualBearing);
    Serial.print(sendStr);
  }

  return true;
}

bool cmdStop() {
  moveStop();
  return true;
}

bool cmdVersion() {
  Serial.println(versionStr);
  return true;
}

void processCommand() {
  bool success = false;
  commandStr.toUpperCase();
  //Serial.print("Command: ");
  //Serial.println(commandStr);
  if (commandStr.charAt(0) == 'A' ) {
    success = cmdPosition();   
  }
  if (commandStr.charAt(0) == ';' ) {
    success = cmdStop();
  }
  if (commandStr.charAt(0) == 'V') {
    success = cmdVersion();
  }
  // Clear command string on CR
  if (commandStr.charAt(0) == 13) {
    success = true;
  }  
  if (success) {
    commandStr.clear();
  }
}

bool readCommand () {
  if (!Serial.available()) { 
    return false; }
  commandStr += Serial.readString();
  return true;
}

// called by controlTimer
static void controlCallback(xTimerHandle pxTimer) {
  //Serial.print("|");
  vTaskResume(controlTaskHandle);
}

/*
 * controlTask is started as a separate task and attached to a specific CPU core.
 * this task is responsible for controlling the rotator
 * the task will execute the contents of the infinte loop once and then suspend itself
 * controlTimer will resume the task every time it expires
 * Note: the execution time of this task is critical, it needs to finish execution
 *   and be suspended before the next trigger via controlTimer.  
 */
void controlTask(void *pvParameters) {
  long tim; // to measure task execution time
  while(1) { // infinite
    
    analogCount = analogRead(ANALOG_PIN);
    analogMilliVolts = map(analogCount,0,1024,0,ANALOG_MAX_MV);
    
    tim = micros();   // time at start
    //
    // control program goes here
    //
    controlTime += (micros() - tim);
    vTaskSuspend(NULL); 
  }
  vTaskDelete(NULL);  // will never execute
}

void loop() {
  if (readCommand()) {
    processCommand();
  }
  vTaskDelay(1);  // hand control to FreeRTOS
  yield(); // let ESP32 background tasks run
  if (nextTimeUpdate < millis()) {
    getLocalTime();
    nextTimeUpdate = millis() + TIME_UPDATE_INTERVAL;
    drawScreen();
  }
}

void getLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  
  strftime(timeHour,3, "%H", &timeinfo);
  strftime(timeMin,3, "%M", &timeinfo);
  strftime(timeSec,3, "%S", &timeinfo);
  strftime(y,5, "%Y", &timeinfo);
  strftime(m,12, "%B", &timeinfo);

  strftime(dw,10, "%A", &timeinfo);
   

   
   strftime(d,3, "%d", &timeinfo);

}
