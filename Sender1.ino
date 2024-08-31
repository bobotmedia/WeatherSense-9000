#include <SPI.h>
#include <iBoardRF24.h>
#include <digitalWriteFast.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <LowPower.h>
#include "printf.h"
#include "SparkFunHTU21D.h"

// devDuino Information
//A0, A1 - displayed on the terminal "Analog" (the other two pins in the connector - VCC and GND for sensor supply)
//D3, D5 - displayed on connector "Digital" (the other two pins in the connector - VCC and GND for sensor supply)
//A4 (SDA), A5 (SCL) - displayed on connector "I2C" (the other two pins in the connector - VCC and GND for sensor supply)
//Interface for connecting an RF-module nRF24L01 +:
//D11 - RF_MOSI
//D12 - RF_MISO
//D13 - RF_SCK
//D8 - RF_CE
//D7 - RF_CSN
//D2 - RF_IRQ
//D4 - Clock button
//D9 - LED
//HTU21D sensor connected to the bus i2c (pins A4 (SDA), A5 (SCL)).


//Use the following constants in call to: "LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);"
//SLEEP_15MS - 15 ms sleep
//SLEEP_30MS - 30 ms sleep
//SLEEP_60MS - 60 ms sleep
//SLEEP_120MS - 120 ms sleep
//SLEEP_250MS - 250 ms sleep
//SLEEP_500MS - 500 ms sleep
//SLEEP_1S - 1 s sleep
//SLEEP_2S - 2 s sleep
//SLEEP_4S - 4 s sleep
//SLEEP_8S - 8 s sleep


// ******* DEFINE CONSTANTS *******
#define SLEEP_TIME_DUR 904  //sleep time in seconds (best if multiple of 8)

// pins for nRF24L01+ transceiver
#define RF_CE 8
#define RF_CSN 7
#define RF_MOSI 11
#define RF_MISO 12
#define RF_SCK 13
#define RF_IRQ 2

// altitude in meters for your location
// needed to accurately calculate relative pressure from absolute pressure
#define myAltitude 153.278


// ******* DECLARE DEVICES and related vars *******

// Declare nRF24L01+ radio using iBoard
// iBoardRF24 replaces standard RF24 library and allows changing pin outs
// to avoid conflicts with other shields, (ie Ethernet)
// iBoardRF24 xxx(CE, CSN, MOSI, MISO, SCK, IRQ)
iBoardRF24 radio(RF_CE, RF_CSN, RF_MOSI, RF_MISO, RF_SCK, RF_IRQ);

// Declare the trans/recv pipe
const uint64_t pipe = 0xE8E8F0F0E1LL;

// Declare sensors structure
struct sensors {             //do not exceed 32 bytes
  byte sender_id;            //1 byte
  unsigned int tx_counter;   //2 bytes
  float temp;                //4 bytes
  float humid;               //4 bytes
  float pres;                //4 bytes
  float battvoltage;         //4 bytes
  boolean resetcount;        //1 byte
};

// Declare payload variable as sensors struct type
sensors payload;

// Declare onboard Temp/Humid sensor
HTU21D myTempHumSensor;

// Declare BMP085 pressure sensor
// Connect SCL to I2C clock - Analog 5 on Arduino Uno 
// Connect SDA to I2C data - Analog 4 on Arduino Uno 
Adafruit_BMP085 BMP085;

float abspressure;   //ABSOLUTE pressure value returned from BMP085
int tx_counter = 0;   //transmit counter

boolean first = true;   //first time this sketch was launched
                        //enables sender to tell receiver to reset rx_counter

int sleep_iter = SLEEP_TIME_DUR / 8;   //divide by 8 here because the lowpower call below is set to 8-second sleeps


void setup(void)
{
  // Print Preamble
  Serial.begin(2400);
  printf_begin();
  printf("\n\rSender\n\r");

  //setup LED on devDuino
  pinMode(9, OUTPUT);
      
  // blink LED so we know it has reset
  for (byte j = 0; j < 3; ++j) {
    digitalWrite(9, HIGH);
    LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_OFF);
    digitalWrite(9, LOW);
    LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_OFF);
  }
            
  // Set up and configure rf radio
  radio.begin();
  radio.setChannel(99);

  // Max power 
  radio.setPALevel( RF24_PA_MAX ) ; 
 
  // Min speed (for better range I presume)
  radio.setDataRate( RF24_250KBPS ) ;

  radio.setRetries(15,15);
  radio.setPayloadSize(32);
  radio.openWritingPipe(pipe);

  payload.sender_id = 0;   //which sender is this?  (0,1,2)  Only sender id 0 (sender1.ino code) has barometric pressure reading due to BMP085 add-on board

  // Set up BMP085 pressure sensor
  BMP085.begin();
  
}


void loop(void)
{
//    Serial.print(F("Batt: "));
    payload.battvoltage = float(readVcc()) / 1000;
//    Serial.println(payload.battvoltage);
    
    payload.temp = ((float)myTempHumSensor.readTemperature()*1.8)+32;  //convert C to F by multiplying by 1.8 and adding 32
//    Serial.print(F("Temperature ("));
//    Serial.write(176);
//    Serial.print(F("F): "));
//    Serial.println(payload.temp, 1);
    
    payload.humid = (float)myTempHumSensor.readHumidity();
//   Serial.print(F("Humidity (%): "));
//    Serial.println(payload.humid, 1);

//    Serial.println(F("Read BMP085 sensor..."));
    abspressure = (float)BMP085.readPressure();   //gets ABSOLUTE pressure from sensor
//    Serial.print(F("Absolute Pressure (inHg): "));
//    Serial.println(abspressure*0.0002953,2);    
  
    payload.pres = (float)BMP085.read_p0(myAltitude, abspressure)*0.0002953;   //converts to RELATIVE pressure based on your location's altitude
//    Serial.print(F("Relative Pressure (inHg): "));
//    Serial.println(payload.pres,2);    

    tx_counter++;
    payload.tx_counter = tx_counter;
//    Serial.print(F("Sending: "));
//    Serial.println(tx_counter);

    // if first time through this sketch, tell recv to reset it's rx_counter too
    if (first) {
      payload.resetcount = true;
      first = false;
    }
    else {
      payload.resetcount = false;
    }

    radio.powerUp();
    LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_OFF);
    bool ok = radio.write(&payload, sizeof(payload));

    // This delay must exist to let read above stabilize before radio power down
    delay(1000);

    radio.powerDown();

//    if (ok)
//      printf("Send OK\n\r");
//    else
//      printf("Send FAILED\n\r");

   delay(250);  //let radio power down before sleep

    // put in sleep mode for chosen no of iterations to conserve battery
    for (byte i = 0; i < sleep_iter; ++i)   
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);  
     
}


// reads batt voltage from devDuino
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(75); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

