/*
Rotator Controller

Board: ESP32 Dev Module
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

#include <WiFi.h>
#include <WiFiClient.h>

const char* host = "esp32";
const char* ssid = "CT";
const char* password = "Siemens300Omron";

const char* versionStr = "VK3ERW rotator";

int actualBearing = 123;
int targetBearing = 189;

bool moveRight = false;
bool moveLeft = false;

String commandStr = "";

void setup() {
  Serial.begin(115200);
  Serial.println("Rotor Controller");

// Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

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

void loop() {
  if (readCommand()) {
    processCommand();
  }

}
