// ==========================================
// LIBRARIES & SENSOR SETUP
// ==========================================
#include <Wire.h>
#include <ACS712.h>
#include <ZMPT101B.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

#define SENSITIVITY 500.0f
SoftwareSerial mySerial(3, 2); // SIM800L pins (RX, TX)

// Initialize sensors and display objects
ZMPT101B voltageSensor(A0, 50.0);
ACS712 ACS(A1, 5.0, 1023, 100);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ==========================================
// GLOBAL VARIABLES
// ==========================================
float workdone = 0.0;
int relayPin = 8;
float voltage;
float units = 0.0;
float balance = 0.5;
bool messageSent = false;
unsigned long lastTime = 0;

// ==========================================
// INITIALIZATION (Runs once on startup)
// ==========================================
void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setBacklight(HIGH);
  
  // Initialize Voltage Sensor & Relay
  voltageSensor.setSensitivity(SENSITIVITY);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Start with load ON

  // Initialize SIM800L Module
  Serial.println("Initializing...");
  delay(1000);

  mySerial.println("AT"); // Handshake test
  updateSerial();
  
  mySerial.println("AT+CMGF=1"); // Set SMS to Text Mode
  updateSerial();
  
  mySerial.println("AT+CNMI=1,2,0,0,0"); // Route incoming SMS directly to serial
  updateSerial();

  // Start the timer for energy calculation
  lastTime = millis(); 
}

// ==========================================
// MAIN LOOP (Runs continuously)
// ==========================================
void loop() {
  float sum = 0;
  float current = 0.0;

  // --- 1. Calculate Time Delta ---
  // Find out how much time has passed since the last loop
  unsigned long currentTime = millis();
  float duration = (currentTime - lastTime) / 1000.0;
  lastTime = currentTime;

  // --- 2. Read Sensors ---
  voltage = voltageSensor.getRmsVoltage();

  // Take 50 samples of current for a stable average
  for (int i = 0; i < 50; i++) {
    sum += ACS.mA_AC();
  }
  current = sum / 50.0;

  // --- 3. Calculate Power & Energy ---
  float power = (voltage * current) / 1000.0; // Power in Watts
  workdone += (power * duration);             // Energy in Watt-seconds (Joules)
  units = workdone / 3600.0;                  // Convert to Watt-hours (Wh)

  // --- 4. Update LCD Display ---
  lcd.setCursor(0,0);
  lcd.print("Voltage (V):");
  lcd.print(voltage);

  lcd.setCursor(0,1);
  lcd.print("Current(mA):");
  lcd.print(current);

  lcd.setCursor(0,2);
  lcd.print("Balance   :");
  lcd.print(balance - units);
  
  lcd.setCursor(0, 3);
  lcd.print("Units(Wh)  :");
  lcd.print(units);

  // --- 5. Balance Check & Relay Control ---
  if (balance - units < 0) {
    // Balance is empty: Cut power
    digitalWrite(relayPin, LOW);
    
    // Send alert SMS (only once)
    if(!messageSent){
      mySerial.println("AT+CMGF=1");
      delay(1000);
      mySerial.println("AT+CMGS=\"+8801567940391\"");
      delay(1000);
      mySerial.print("Not Sufficient Balance");
      delay(100);
      mySerial.write(26); // Send CTRL+Z
      delay(5000);

      messageSent = true;
    }
  } else {
    // Balance is positive: Keep power on
    digitalWrite(relayPin, HIGH);
  }
  
  // --- 6. Listen for Recharge SMS ---
  updateSerialWithFloatCheck(); 
}

// ==========================================
// HELPER FUNCTIONS
// ==========================================

// Forward data between Hardware Serial and Software Serial (SIM800L)
void updateSerial() {
  delay(500);
  while (Serial.available()) {
    mySerial.write(Serial.read());
  }
  while(mySerial.available()) {
    Serial.write(mySerial.read());
  }
}

// Read incoming SMS and parse for "recharge: XX" commands
void updateSerialWithFloatCheck() {
  delay(500);
  String readString;
  
  // Pass manual commands to SIM800L
  while (Serial.available()) {
    mySerial.write(Serial.read());
  }

  // Read output from SIM800L
  while(mySerial.available()) {
    char c = mySerial.read();
    
    // Build the string character by character until a newline
    if(c != '\n' && c != '\r') {
      readString += c;
    } else if(readString.length() > 0) {
      
      // Clean up the line of text
      readString.trim();
      readString.toLowerCase();
      
      // Check if this line is a recharge command
      if(readString.startsWith("recharge:")) {
        String valString = readString.substring(9); // Extract the number part
        float tempFloat = valString.toFloat();
        
        // If a valid amount was found, add it to the balance
        if(tempFloat > 0.0) {
          balance += tempFloat;
          messageSent = false; // Reset the SMS alert flag
          Serial.println(tempFloat); // Print to serial monitor for debugging
        }
      }
      readString = ""; // Reset string for the next line
    }
  }
}