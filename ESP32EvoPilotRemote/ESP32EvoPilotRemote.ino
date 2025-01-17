/*
  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// NMEA2000 Remote Control for EV-1
//   - Reads 433 MHz commands via RXB6 receiver
//   - Sends NMEA2000 messages to EV-1 Course Computer

// Version 0.7, 29.08.2021, AK-Homberger

#define ESP32_CAN_TX_PIN GPIO_NUM_5  // Set CAN TX port to 5 
#define ESP32_CAN_RX_PIN GPIO_NUM_4  // Set CAN RX port to 4

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <N2kMsg.h>
#include <NMEA2000.h>
#include <NMEA2000_CAN.h>
#include <RCSwitch.h>

#include "RaymarinePilot.h"
#include "N2kDeviceList.h"

#define ESP32_RCSWITCH_PIN GPIO_NUM_15  // Set RCSWITCH port to 15 (RXB6 receiver)
#define KEY_DELAY 300  // 300 ms break between keys
#define BEEP_TIME 200  // 200 ms beep time

#define BUZZER_PIN 2  // Buzzer connected to GPIO 2

#define PARENTS 0
#define THOMAS 1
#define GALIENNE 0
int NodeAddress;  // To store last Node Address

Preferences preferences;             // Nonvolatile storage on ESP32 - To store LastDeviceAddress

RCSwitch mySwitch = RCSwitch();

unsigned long key_time = 0;
unsigned long beep_time = 0;
bool beep_status = false;

#ifdef PARENT
const unsigned long Remote1_Key_Minus_1 = 8298284;
const unsigned long Remote1_Key_Plus_1 = 8298274;
const unsigned long Remote1_Key_Minus_10 = 8298282;
const unsigned long Remote1_Key_Plus_10 = 8298278;
const unsigned long Remote1_Key_Auto = 8298276;
const unsigned long Remote1_Key_Standby = 8298280;
const unsigned long Remote1_Key_Tack_PortSide= 1234; // Babord
const unsigned long Remote1_Key_Tack_Starboard= 1234; // Tribord
const unsigned long Remote2_Key_Minus_1 = 1913964;
const unsigned long Remote2_Key_Plus_1 = 1913954;
const unsigned long Remote2_Key_Minus_10 = 1913962;
const unsigned long Remote2_Key_Plus_10 = 1913958;
const unsigned long Remote2_Key_Auto = 1913956;
const unsigned long Remote2_Key_Standby = 1913960;
const unsigned long Remote2_Key_Tack_PortSide= 1913966; // Babord
const unsigned long Remote2_Key_Tack_Starboard= 1913953; // Tribord
#elif THOMAS
const unsigned long Remote1_Key_Minus_1 = 12240684;
const unsigned long Remote1_Key_Plus_1 = 12240674;
const unsigned long Remote1_Key_Minus_10 = 12240682;
const unsigned long Remote1_Key_Plus_10 = 12240678;
const unsigned long Remote1_Key_Auto = 12240676;
const unsigned long Remote1_Key_Standby = 12240680;
const unsigned long Remote1_Key_Tack_PortSide= 12240686; // Babord
const unsigned long Remote1_Key_Tack_Starboard= 12240673; // Tribord
const unsigned long Remote2_Key_Minus_1 = 9178220;
const unsigned long Remote2_Key_Plus_1 = 9178210;
const unsigned long Remote2_Key_Minus_10 = 9178218;
const unsigned long Remote2_Key_Plus_10 = 9178214;
const unsigned long Remote2_Key_Auto = 9178212;
const unsigned long Remote2_Key_Standby = 9178216;
const unsigned long Remote2_Key_Tack_PortSide= 9178222; // Babord
const unsigned long Remote2_Key_Tack_Starboard= 9178209; // Tribord
#elif GALIENNE
const unsigned long Remote1_Key_Minus_1 = 8949036;
const unsigned long Remote1_Key_Plus_1 = 8949026;
const unsigned long Remote1_Key_Minus_10 = 8949034;
const unsigned long Remote1_Key_Plus_10 = 8949030;
const unsigned long Remote1_Key_Auto = 8949028;
const unsigned long Remote1_Key_Standby = 8949032;
const unsigned long Remote1_Key_Tack_PortSide= 8949038; // Babord
const unsigned long Remote1_Key_Tack_Starboard= 8949025; // Tribord
const unsigned long Remote2_Key_Minus_1 = 7483500;
const unsigned long Remote2_Key_Plus_1 = 7483490;
const unsigned long Remote2_Key_Minus_10 = 7483498;
const unsigned long Remote2_Key_Plus_10 = 7483494;
const unsigned long Remote2_Key_Auto = 7483492;
const unsigned long Remote2_Key_Standby = 7483496;
const unsigned long Remote2_Key_Tack_PortSide= 7483502; // Babord
const unsigned long Remote2_Key_Tack_Starboard= 7483489; // Tribord
#endif

const unsigned long TransmitMessages[] PROGMEM = {126208UL,   // Set Pilot Mode
                                                  126720UL,   // Send Key Command
                                                  65288UL,    // Send Seatalk Alarm State
                                                  0
                                                 };

const unsigned long ReceiveMessages[] PROGMEM = { 127250UL,   // Read Heading
                                                  65288UL,    // Read Seatalk Alarm State
                                                  65379UL,    // Read Pilot Mode
                                                  0
                                                };

tN2kDeviceList *pN2kDeviceList;
short pilotSourceAddress = -1;


void setup() {
  uint8_t chipid[6];
  uint32_t id = 0;
  int i = 0;
  
  WiFi.mode(WIFI_OFF);
  btStop();

  esp_efuse_mac_get_default(chipid);
  for (i = 0; i < 6; i++) id += (chipid[i] << (7 * i));

  // Reserve enough buffer for sending all messages. This does not work on small memory devices like Uno or Mega
  NMEA2000.SetN2kCANReceiveFrameBufSize(150);
  NMEA2000.SetN2kCANMsgBufSize(8);
  // Set Product information
  NMEA2000.SetProductInformation("00000001", // Manufacturer's Model serial code
                                 100, // Manufacturer's product code
                                 "Evo Pilot Remote",  // Manufacturer's Model ID
                                 "1.0.0.0",  // Manufacturer's Software version code
                                 "1.0.0.0" // Manufacturer's Model version
                                );
  // Set device information
  NMEA2000.SetDeviceInformation(id, // Unique number. Use e.g. Serial number.
                                132, // Device function=Analog to NMEA 2000 Gateway. See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                                25, // Device class=Inter/Intranetwork Device. See codes on  http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                                2046 // Just choosen free from code list on http://www.nmea.org/Assets/20121020%20nmea%202000%20registration%20list.pdf
                               );

  Serial.begin(115200);
  delay(100);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  mySwitch.enableReceive(ESP32_RCSWITCH_PIN);  // Receiver on GPIO15 on ESP32

  // Uncomment 3 rows below to see, what device will send to bus
  // NMEA2000.SetForwardStream(&Serial);  // PC output on due programming port
  // NMEA2000.SetForwardType(tNMEA2000::fwdt_Text); // Show in clear text. Leave uncommented for default Actisense format.
  // NMEA2000.SetForwardOwnMessages();

  preferences.begin("nvs", false);                          // Open nonvolatile storage (nvs)
  NodeAddress = preferences.getInt("LastNodeAddress", 34);  // Read stored last NodeAddress, default 34
  preferences.end();
  Serial.printf("NodeAddress=%d\n", NodeAddress);

  // If you also want to see all traffic on the bus use N2km_ListenAndNode instead of N2km_NodeOnly below
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, NodeAddress); //N2km_NodeOnly N2km_ListenAndNode
  NMEA2000.ExtendTransmitMessages(TransmitMessages);
  NMEA2000.ExtendReceiveMessages(ReceiveMessages);

  NMEA2000.SetMsgHandler(RaymarinePilot::HandleNMEA2000Msg);

  pN2kDeviceList = new tN2kDeviceList(&NMEA2000);
  //NMEA2000.SetDebugMode(tNMEA2000::dm_ClearText); // Uncomment this, so you can test code without CAN bus chips on Arduino Mega
  NMEA2000.EnableForward(false); // Disable all msg forwarding to USB (=Serial)
  NMEA2000.Open();

  Serial.println((String) "NMEA2000 Open");
}


// Beep on if key received

void BeepOn() {
  if (beep_status == true) return;  // Already On

  digitalWrite(BUZZER_PIN, HIGH);
  beep_time = millis();
  beep_status = true;
}


// Beep off after BEEP_TIME

void BeepOff() {
  if (beep_status == true && millis() > beep_time + BEEP_TIME) {
    digitalWrite(BUZZER_PIN, LOW);
    beep_status = false;
  }
}


// Get device source address (of EV-1)

int getDeviceSourceAddress(String model) {
  if (!pN2kDeviceList->ReadResetIsListUpdated()) return -1;
  for (uint8_t i = 0; i < N2kMaxBusDevices; i++) {
    const tNMEA2000::tDevice *device = pN2kDeviceList->FindDeviceBySource(i);
    if ( device == 0 ) continue;

    String modelVersion = device->GetModelVersion();

    if (modelVersion.indexOf(model) >= 0) {
      return device->GetSource();
    }
  }
  return -2;
}


// Receive 433 MHz commands from remote and send SeatalkNG codes to EV-1 (if available)
void Handle_AP_Remote(void) {
  unsigned long key = 0;

  if (pilotSourceAddress < 0) pilotSourceAddress = getDeviceSourceAddress("EV-1"); // Try to get EV-1 Source Address

  if (mySwitch.available()) {
    key = mySwitch.getReceivedValue();
    mySwitch.resetAvailable();
  }

  if (key > 0 && millis() > key_time + KEY_DELAY) {
    key_time = millis();   // Remember time of last key received
    // Serial.println(key);

    if (key == Remote1_Key_Standby || key == Remote2_Key_Standby) {
      Serial.println("Setting PILOT_MODE_STANDBY");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, pilotSourceAddress, PILOT_MODE_STANDBY);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Auto || key == Remote2_Key_Auto) {
      Serial.println("Setting PILOT_MODE_AUTO");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, pilotSourceAddress, PILOT_MODE_AUTO);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Plus_1 || key == Remote2_Key_Plus_1) {
      Serial.println("+1");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_PLUS_1);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Plus_10 || key == Remote2_Key_Plus_10) {
      Serial.println("+10");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_PLUS_10);
      NMEA2000.SendMsg(N2kMsg);
    }
    
    else if (key == Remote1_Key_Minus_1 || key == Remote2_Key_Minus_1) {
      Serial.println("-1");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_MINUS_1);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Minus_10 || key == Remote2_Key_Minus_10) {
      Serial.println("-10");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_MINUS_10);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Tack_PortSide || key == Remote2_Key_Tack_PortSide) {
      Serial.println("Tack Port Side");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_TACK_PORTSIDE);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if (key == Remote1_Key_Tack_Starboard || key == Remote2_Key_Tack_Starboard) {
      Serial.println("Tack Starboard");
      BeepOn();
      if (pilotSourceAddress < 0) return; // No EV-1 detected. Return!
      tN2kMsg N2kMsg;
      RaymarinePilot::KeyCommand(N2kMsg, pilotSourceAddress, KEY_TACK_STARBORD);
      NMEA2000.SendMsg(N2kMsg);
    }
  }
  BeepOff();
}

void loop() {
  Handle_AP_Remote();
  NMEA2000.ParseMessages();

  int SourceAddress = NMEA2000.GetN2kSource();
  if (SourceAddress != NodeAddress) { // Save potentially changed Source Address to NVS memory
    NodeAddress = SourceAddress;      // Set new Node Address (to save only once)
    preferences.begin("nvs", false);
    preferences.putInt("LastNodeAddress", SourceAddress);
    preferences.end();
    Serial.printf("Address Change: New Address=%d\n", SourceAddress);
  }

}
