/*------------------------------------------------------------------------------ 
Program:      ir_remote_control_converter 

Description:  infrared remote control (IR RC) converter that allows emulating an 
              IR remote control (emulated IR RC) using a different one 
              (physical IR RC). 
              IR codes sent by the physical IR RC are intercepted by an IR receiver, 
              translated into the physical IR RC corresponding codes and sent 
              through an IR transmitter. The translation table that maps the 
              physical to emulated IR RC codes is permanently stored in the 
              microcontroller's internal EEPROM; it can be (re)initialized 
              by putting the system in setup mode (hardware interrupt generated 
              by a button press).

Hardware:     Arduino Uno R3, IR transmitter, IR receiver. Should work 
              with other Arduinos 

Software:     Developed using Arduino 1.8.2 IDE

Libraries:    
              - Arduino-IRremote: https://github.com/z3t0/Arduino-IRremote
              - EEPROM library (included with the Arduino IDE)

References: 
              - Keyes Infrared Transmitter Module datasheet: 
				http://tinkbox.ph/sites/tinkbox.ph/files/downloads/Keyes%20-%20Infrared%20Transmitter%20Module.pdf
              - Keyes Infrared Receiver Module datasheet: 
                http://tinkbox.ph/sites/tinkbox.ph/files/downloads/Keyes%20-%20Infrared%20Receiver%20Module%20.pdf

Date:         May 18, 2017

Author:       Gheorghe G., http://www.arduinolab.net
------------------------------------------------------------------------------*/

#include <boarddefs.h>
#include <IRremote.h>
#include <IRremoteInt.h>
#include <EEPROM.h>

// ================================================================
// timming correction required for samsung header in ir_Samsung.cpp (Arduino-IRremote library)
// //#define SAMSUNG_HDR_MARK    5000
// //#define SAMSUNG_HDR_SPACE   5000
// #define SAMSUNG_HDR_MARK    4450
// #define SAMSUNG_HDR_SPACE   4450
// ================================================================

// hardware parameters
int pinIRReceiver = 11;   // pin the ir receiver is connected to
int pinSetupButton = 2;   // pin the setup mode switch is connected to
int pinBuiltinLED = 13;   // internal led (on in setup mode)

// ir receiver parameters
const int NUMBER_OF_KEYS = 15;                // number of used ir remote keys
unsigned long irKeyCodes[NUMBER_OF_KEYS][2];  // use unsigned long, some remote ir codes do not fit into long
String irKeyNames[NUMBER_OF_KEYS] = {"on/off", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "vol-", "vol+", "ch-", "ch+"}; // names of used ir remote keys

// eeprom parameters
const byte EEPROM_ID_VALUE = 0x99;                                      // id value (used for data validation)
const byte EEPROM_ID_ADDRESS = 0;                                       // id address
const byte EEPROM_PHY_IR_TYPE_ADDRESS = EEPROM_ID_ADDRESS + 1;          // physical ir remote type address
const byte EEPROM_EMU_IR_TYPE_ADDRESS = EEPROM_PHY_IR_TYPE_ADDRESS + 1; // emulated ir remote type address
const byte EEPROM_DATA_ADDRESS = EEPROM_EMU_IR_TYPE_ADDRESS + 1;        // data storage (ir remotes key codes) address

// eeprom id value
byte eepromIdValue = 0;

// physical and emulated ir remote types
byte phyIrType = 0;
byte emuIrType = 0;

// setup mode flag (true = setup mode; false = operation mode)
volatile bool setupMode = false;

// create an IRrecv object
IRrecv irrecv(pinIRReceiver);
//stores results from ir receiver
decode_results decodedSignal;

// create an IRsend object
IRsend irsend;

void setup() {
  pinMode(pinIRReceiver, INPUT); // input from ir receiver
  
  pinMode(pinSetupButton, INPUT); // input from setup mode switch
  digitalWrite(pinSetupButton, HIGH); // turn on internal pullup
  
  //attachInterrupt(0, setupModeOn, FALLING); // interrupt when setup mode switch pressed
  attachInterrupt(0, setupModeOn, LOW); // interrupt when setup mode switch pressed
  
  pinMode(pinBuiltinLED, OUTPUT); // internal led to be used for status
  digitalWrite(pinBuiltinLED, LOW); // setup mode off, led off

  irrecv.enableIRIn(); // start the receiver object
  
  Serial.begin(9600); // enable serial for debugging

  // check eeprom id; if valid, load eeprom data in ram; if not, enable setup mode 
  eepromIdValue = eepromReadByte(EEPROM_ID_ADDRESS);
  delay(50);
  if (eepromIdValue == EEPROM_ID_VALUE) {
    // valid eeprom id, setup mode off
    Serial.print("eeprom id: ");
    Serial.println(eepromIdValue, HEX);
    Serial.println("");

    phyIrType = eepromReadByte(EEPROM_PHY_IR_TYPE_ADDRESS);
    delay(50);
    Serial.print("phy remote type: "); // remote types defined in IRremote.h (Arduino-IRremote library)
    Serial.println(phyIrType - 1);

    emuIrType = eepromReadByte(EEPROM_EMU_IR_TYPE_ADDRESS);
    delay(50);
    Serial.print("emu remote type: "); // remote types defined in IRremote.h (Arduino-IRremote library)
    Serial.println(emuIrType - 1);

    eepromGetData (EEPROM_DATA_ADDRESS, &irKeyCodes[0], NUMBER_OF_KEYS);
    printCodeKeyMapping(&irKeyCodes[0], &irKeyNames[0], NUMBER_OF_KEYS);
  }
  else {
    setupMode = true;
  }
}

void loop() {
  unsigned long rcvValue = 0;

  if (setupMode == true) {
    Serial.println("");
    Serial.println("setup mode on");
    digitalWrite(pinBuiltinLED, HIGH); // enable setup mode, led on
    Serial.print("erasing eeprom ... ");
    eepromErase(); // erase eeprom content
    Serial.println("done");
    Serial.print("programming eeprom id ... ");
    eepromWriteByte(EEPROM_ID_VALUE, EEPROM_ID_ADDRESS); // write eeprom id
    eepromIdValue = eepromReadByte(EEPROM_ID_ADDRESS);
    Serial.println(eepromIdValue, HEX);

    Serial.println("first (physical) ir remote");
    phyIrType = learnIRKeyCodes(&irKeyNames[0], &irKeyCodes[0], NUMBER_OF_KEYS, 0); // learn key codes and store them in ram
    eepromWriteByte(phyIrType, EEPROM_PHY_IR_TYPE_ADDRESS);
    Serial.print("phy remote type: ");
    Serial.println(eepromReadByte(EEPROM_PHY_IR_TYPE_ADDRESS) - 1); // save physical remote type to eeprom

    Serial.println("second (emulated) ir remote");
    emuIrType = learnIRKeyCodes(&irKeyNames[0], &irKeyCodes[0], NUMBER_OF_KEYS, 1); // learn key codes and store them in ram
    eepromWriteByte(emuIrType, EEPROM_EMU_IR_TYPE_ADDRESS);
    Serial.print("emu remote type: ");
    Serial.println(eepromReadByte(EEPROM_EMU_IR_TYPE_ADDRESS) - 1); // save emulated remote type to eeprom

    eepromSetData (EEPROM_DATA_ADDRESS, &irKeyCodes[0], NUMBER_OF_KEYS); // save key codes from ram to eeprom
    Serial.println("learning complete, data saved to eeprom");
    setupMode = false; // disable setup mode
    Serial.println("setup mode off");
    printCodeKeyMapping(&irKeyCodes[0], &irKeyNames[0], NUMBER_OF_KEYS); // print eeprom data from ram
    digitalWrite(pinBuiltinLED, LOW); // setup mode off, led off
    Serial.println("");
  }
  if (irrecv.decode(&decodedSignal)) {
    rcvValue = decodedSignal.value;
    delay(200); // mandatory between receive - transmit (experimental value)
  }
  if (rcvValue > 0) {
    Serial.print(rcvValue, HEX);
    Serial.print(" | ");
    int i = 0;
    while (i < NUMBER_OF_KEYS) {
      if (irKeyCodes[i][0] == rcvValue) {
        Serial.print(irKeyCodes[i][1], HEX);
        Serial.print(" | ");
        Serial.println(irKeyNames[i]);

        switch (emuIrType - 1) {
          default:
          /*case RC5:     irsend.sendRC5 (unsigned long data, int nbits);   break;
          case RC6:       irsend.sendRC6 (unsigned long data, int nbits);   break;
          case NEC:       irsend.sendNEC (unsigned long data, int nbits);   break;
          case SONY:      irsend.sendSony (unsigned long data, int nbits);  break;
          case PANASONIC: irsend.sendPanasonic (unsigned int address, unsigned long data);  break;*/
          case JVC:       irsend.sendJVC(irKeyCodes[i][1],16,0);            break;
          case SAMSUNG:   irsend.sendSAMSUNG(irKeyCodes[i][1],32);          break;
          /*case WHYNTER: irsend.sendWhynter (unsigned long data, int nbits); break;
          case AIWA_RC_T501:  irsend.sendAiwaRCT501 (int code);             break;
          case LG:        irsend.sendLG (unsigned long data, int nbits);    break;
          case DISH:      irsend.sendDISH (unsigned long data, int nbits);  break;
          case SHARP:     irsend.sendSharp (unsigned int address, unsigned int command);  break;
          case DENON:     irsend.sendDenon (unsigned long data, int nbits); break;*/
        }

        delay(50);
        irrecv.enableIRIn();
        break;
      }
      i++;
    }
    irrecv.resume();
  }
}

byte learnIRKeyCodes(String irKeyNamesArray[], unsigned long irKeyCodesArray[][2], int numberOfKeys, int dataSetRow) {
  while (irrecv.decode(&decodedSignal)) //empty the buffer
    irrecv.resume();
  int remoteType = -1;

  Serial.println("start learning ir remote key codes");

  unsigned long previousValue = -1;
  int i = 0;
  while (i < numberOfKeys)
  {
    Serial.print("key: ");
    Serial.print(irKeyNamesArray[i]);
    while (true)
    {
      if (irrecv.decode(&decodedSignal)) {
        if (decodedSignal.value != -1 && decodedSignal.value != previousValue) {
          if (i == 0) {
            remoteType = decodedSignal.decode_type;
          }
          irKeyCodesArray[i][dataSetRow] = decodedSignal.value;
          previousValue = decodedSignal.value;
          Serial.print(" code: ");
          Serial.println(decodedSignal.value, HEX);
          i++;
          delay(300);
          irrecv.resume();
          break;
        }
        delay(300);
        irrecv.resume();
      }
    }
  }
  return (remoteType + 1);
}

void eepromWriteLong(unsigned long valueToWrite, int eepromAddress) {
  // decompose a long (4 bytes) to 4 bytes, byte1 = MSB, byte4 = LSB
  byte byte4 = (valueToWrite & 0xFF);
  byte byte3 = ((valueToWrite >> 8) & 0xFF);
  byte byte2 = ((valueToWrite >> 16) & 0xFF);
  byte byte1 = ((valueToWrite >> 24) & 0xFF);

  // write the 4 bytes into eeprom
  EEPROM.write(eepromAddress, byte4);
  EEPROM.write(eepromAddress + 1, byte3);
  EEPROM.write(eepromAddress + 2, byte2);
  EEPROM.write(eepromAddress + 3, byte1);
}

unsigned long eepromReadLong(unsigned long eepromAddress) {
  // read 4 bytes from eeprom
  unsigned long byte4 = EEPROM.read(eepromAddress);
  unsigned long byte3 = EEPROM.read(eepromAddress + 1);
  unsigned long byte2 = EEPROM.read(eepromAddress + 2);
  unsigned long byte1 = EEPROM.read(eepromAddress + 3);

  // build the long value from the 4 bytes
  return ((byte4 << 0) & 0xFF) + ((byte3 << 8) & 0xFFFF) + ((byte2 << 16) & 0xFFFFFF) + ((byte1 << 24) & 0xFFFFFFFF);
}

void eepromErase() {
  for (int i = 0 ; i < EEPROM.length() ; i++)
    EEPROM.write(i, 0);
}

byte eepromReadByte (byte eepromAddress) {
  return (EEPROM.read(eepromAddress));
}

void eepromWriteByte ( byte valueToWrite, byte eepromAddress) {
  return (EEPROM.write(eepromAddress, valueToWrite));
}

void eepromGetData (byte eepromDataAddress, unsigned long longValuesArray[][2], int numberOfValues) {
  for (int i = 0; i < numberOfValues; i++) {
    longValuesArray[i][0] = eepromReadLong((eepromDataAddress + 8 * i));
    longValuesArray[i][1] = eepromReadLong((eepromDataAddress + 8 * i + 4));
  }
}

void eepromSetData (byte eepromDataAddress, unsigned long longValuesArray[][2], int numberOfValues) {
  for (int i = 0; i < numberOfValues; i++) {
    eepromWriteLong(longValuesArray[i][0], eepromDataAddress + 8 * i);
    eepromWriteLong(longValuesArray[i][1], eepromDataAddress + 8 * i + 4);
  }
}

// print mapping codes - keys
void printCodeKeyMapping(unsigned long irKeyCodesArray[][2], String irKeyNamesArray[], int numberOfKeys) {
  Serial.println("");
  Serial.println("phy key code | emu key code2 | key name");
  Serial.println("--------------------------------");
  for (int i = 0; i < numberOfKeys; i++) {
    Serial.print(irKeyCodesArray[i][0], HEX);
    Serial.print (" | ");
    Serial.print(irKeyCodesArray[i][1], HEX);
    Serial.print (" | ");
    Serial.println(irKeyNamesArray[i]);
  }
  Serial.println("--------------------------------");
  Serial.println("");
}

void setupModeOn() {
  setupMode = true;
}
