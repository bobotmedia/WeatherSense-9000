#include <SPI.h>
#include <iBoardRF24.h>
#include <Ethernet.h>
#include <digitalWriteFast.h>
#include "printf.h"
#include <Xively.h>
#include <HttpClient.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <DS1307RTC.h>
#include <Time.h>
#include <Timezone.h>    //https://github.com/JChristensen/Timezone


// ******* DECLARE DEVICES and related vars and constants *******

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);

TimeChangeRule *tcr;   //pointer to the time change rule, use to get TZ abbrev
time_t localtime;      //holds local time based on DST rules  

// Declare LCD 20x4 with AdaFruit I2C backpack
// The shield uses the I2C SCL and SDA pins
LiquidCrystal lcd(0);

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; //unique MAC address found on the back of your ethernet shield
IPAddress ip(192,168,1,79); // IP address dependent upon your network addresses.

// IP address of the remote display (remote server) which this main recv will send to as a client
IPAddress remoteserver(192,168,1,78);

// Plumbing for syncing RTC with NTP time server periodically
IPAddress timeServer(131,107,13,100);   // NTP server
#define localPort 8888                  // local port to listen for UDP packets. needed for the NTP servers for time updating
#define NTP_PACKET_SIZE 48              // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];    //buffer to hold incoming and outgoing packets
EthernetUDP Udp;                        // A UDP instance to let us send and receive packets over UDP
unsigned long epoch;
boolean timesetdone = false;  //flags that RTC was set from NTP

// Initialize the Ethernet server library
// with the IP address and port you want to use
// (port 80 is default for HTTP):
EthernetServer server(7979);

// refresh web page every xx seconds
#define WEBPAGE_REFRESH_MINS 5

// pins for nRF24L01+ transceiver
#define RF_CE 3
#define RF_CSN 8
#define RF_MOSI 5
#define RF_MISO 6
#define RF_SCK 7
#define RF_IRQ 2

// Declare nRF24L01+ transceiver radio using iBoard
// iBoard replaces standard RF24 library and allows changing pin outs
// to avoid conflicts with shields, (ie Ethernet)
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

// Your Xively key to let you upload data
char xivelyKey[] = "---------YOUR UNIQUE XIVELY API KEY HERE-----------";

// Your xively feed ID
#define xivelyFeed xxxxxxxxxx   //where 'xxx' is your your unique Xively Feed ID

// Declare Xively datastreams
char tempID_Porch[] = "Temperature_Porch";
char humidID_Porch[] = "Humidity_Porch";
char presID_Porch[] = "Pressure_Porch";
char battID_Porch[] = "Battery_Porch";

char tempID_Garage[] = "_Temperature_Garage";
char humidID_Garage[] = "_Humidity_Garage";
char battID_Garage[] = "_Battery_Garage";

char tempID_Basement[] = "__Temp_Basemnt";
char humidID_Basement[] = "__Humidity_Basemnt";
char battID_Basement[] = "__Battery_Basemnt";

// Declare strings for Xively datastream IDs
XivelyDatastream datastreams_porch[] = {
  XivelyDatastream(tempID_Porch, strlen(tempID_Porch), DATASTREAM_FLOAT),
  XivelyDatastream(humidID_Porch, strlen(humidID_Porch), DATASTREAM_FLOAT),
  XivelyDatastream(presID_Porch, strlen(presID_Porch), DATASTREAM_FLOAT),
  XivelyDatastream(battID_Porch, strlen(battID_Porch), DATASTREAM_FLOAT),
};

XivelyDatastream datastreams_garage[] = {
  XivelyDatastream(tempID_Garage, strlen(tempID_Garage), DATASTREAM_FLOAT),
  XivelyDatastream(humidID_Garage, strlen(humidID_Garage), DATASTREAM_FLOAT),
  XivelyDatastream(battID_Garage, strlen(battID_Garage), DATASTREAM_FLOAT),
};

XivelyDatastream datastreams_basement[] = {
  XivelyDatastream(tempID_Basement, strlen(tempID_Basement), DATASTREAM_FLOAT),
  XivelyDatastream(humidID_Basement, strlen(humidID_Basement), DATASTREAM_FLOAT),
  XivelyDatastream(battID_Basement, strlen(battID_Basement), DATASTREAM_FLOAT),
};

// Finally, wrap the datastreams into a feed
XivelyFeed feed_porch(xivelyFeed, datastreams_porch, 4 /* number of datastreams */);
XivelyFeed feed_garage(xivelyFeed, datastreams_garage, 3 /* number of datastreams */);
XivelyFeed feed_basement(xivelyFeed, datastreams_basement, 3 /* number of datastreams */);

// Declare vars for RTC values
tmElements_t tm;
byte rtcmonth;
byte rtcday;
byte rtcyear;
byte rtchour;
byte rtcminute;
byte rtcsecond;

// holds current date time from RTC as char array
char strdatetime[3][20];

// holds current date time for xmit to remote node
char strdatetimexmit[12];

// hold short date/time for LCD display
char strdatetimeshort[3][12];

// Declare payload variable as sensors struct type
sensors temppayload;
sensors payload[3];

// Declare string vars for printing
char strsenderid[3] = "-";
char strtemp[3][6] = {"--","--","--"};
char strhumid[3][6] = {"--","--","--"};
char strpres[3][6] = {"--","--","--"};
char strvolt[3][6] = {"--","--","--"};
char strsent[3][6] = {"--","--","--"};
char strrecd[3][6] = {"--","--","--"};
char strupdtime[6] = "-";
char mystring[3][50] = {"--","--","--"};

int rx_counter[3] = {0,0,0};
volatile boolean newpayload = false;

boolean printstar = false;   //print star on LCD indicating time was updated

byte senderid = 0;   //0 is Porch sender node, 1 is Garage sender node, 2 is Basement node
boolean updatedisplay = false;   //force display to update?
byte showscreen = 0;

unsigned long Timer;  //"ALWAYS use unsigned long for timers, not int"
unsigned long timedelay = 30000;  //how often to change display between screens
unsigned long timepassed = 0;  

// Interrupt handler, check the radio because we got an IRQ
void check_radio(void);


void setup(void)
{
  // Print Preamble
  Serial.begin(9600);
  printf_begin();
  Serial.println(F("Receiver"));

  // set up the LCD with number of columns and rows
  lcd.setBacklight(LOW);
  lcd.begin(20, 4);
  lcd.clear();
  lcd.print(F("Initializing..."));
  
  // Setup and configure rf radio
  radio.begin();
  radio.setChannel(99);

  // Max power 
  radio.setPALevel( RF24_PA_MAX ) ; 
 
  // Min speed (for better range I presume)
  radio.setDataRate( RF24_250KBPS ) ;
  
  radio.setRetries(15,15);
  radio.setPayloadSize(32);
  radio.openReadingPipe(1,pipe);
  radio.startListening();

  // Attach interrupt handler to interrupt #0 (using pin 2)  
  attachInterrupt(0, check_radio, FALLING);
  
  // start the Ethernet connection and the server:
  Ethernet.begin(mac, ip);
  server.begin();
//  Serial.print(F("Server is at "));
//  Serial.println(Ethernet.localIP());

  Udp.begin(localPort);   // for NTP time server

  delay(2000);

  // Try to retreive date and time from NTP and set RTC
  SetTime();

  setSyncProvider(RTC.get);

  if(timeStatus()!= timeSet) 
    Serial.println("Unable to sync with the RTC");
  else
  {
    Serial.println("RTC active");
    lcd.setCursor(0, 2);
    lcd.print(F("RTC active"));
  }

  // read and spew RTC to serial output and LCD
  if (RTC.read(tm)) 
  {
      //convert UTC to local time
      localtime = makeTime(tm);

      // break down values from RTC read
      rtcmonth = month(localtime);
      rtcday = day(localtime);
      rtcyear = year(localtime)-2000;
      rtchour = hour(localtime);
      rtcminute = minute(localtime);
      rtcsecond = second(localtime);

      Serial.print(rtcmonth);
      Serial.print("/");
      Serial.print(rtcday);
      Serial.print("/");
      Serial.print(rtcyear);
      Serial.print(" ");
      Serial.print(rtchour);
      Serial.print(":");
      Serial.print(rtcminute);
      Serial.print(":");
      Serial.println(rtcsecond);
      
      //Spew time stamp to LCD
      char strtmp[20];
      sprintf(strtmp, "%02d/%02d/%02d %02d:%02d:%02d",rtcmonth,rtcday,rtcyear,rtchour,rtcminute,rtcsecond);
      lcd.setCursor(0, 3);
      lcd.print(strtmp);
     
  }

  // Timer init for LCD update         
  Timer = millis();

}

void loop(void)
{
  // Nothing in here about radio receive...flag set in interrupt handler "check_radio" below
  
  // use millis and a timer instead of delay so loop keeps responding to web server requests
  timepassed = millis()-Timer;   //how long has passed since Timer was set

  //update display if desired time has passed or if force to update
  if ((timepassed >= timedelay) || (updatedisplay))
  {
    switch (showscreen) {
      case 0:
        updatelcd(0);
        break;
      case 1:
        updatelcd(1);
        break;
      case 2:
        updatelcd(2);
        break;
    }
    showscreen++;
    if (showscreen == 3) showscreen = 0;
    updatedisplay=false;

    Timer = millis();
  }


  // Respond to web requests in here to avoid web server hanging waiting on interrupt
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    Serial.print(F("Req->"));
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      delay(5);   //keeps web server from hanging
      if (client.available()) {
        char c = client.read();
        //Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {

          // send a standard http response header
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: text/html"));
          client.println(F("Connection: close"));  // the connection will be closed after completion of the response
          client.print(F("Refresh: "));  // refresh the page automatically every x sec
          client.println(WEBPAGE_REFRESH_MINS*60);
          client.println();
          client.println(F("<!DOCTYPE HTML>"));
          client.println(F("<html>"));
          client.println(F("<title>My Weather Readings</title>"));

          client.println(F("<head>"));
          client.println(F("<style>"));
          client.println(F("table, th, td {"));
          client.println(F("font-size:26px;"));
          client.println(F("border: 1px solid black;"));
          client.println(F("border-collapse: collapse;"));
          client.println(F("}"));
          client.println(F("th, td {"));
          client.println(F("padding: 15px;"));
          client.println(F("}"));
          client.println(F("th, td {"));
          client.println(F("text-align: center;"));
          client.println(F("}"));
          client.println(F("</style>"));
          client.println(F("</head>"));

 
          // output the sensor values
          client.println(F(""));
          client.println(F("<body>"));


          // ****   PORCH readings on Web Page   **** //

          client.println(F("<table>"));
          //----------------------------------------------
          client.println(F("<caption>My Porch</caption>"));
          client.println(F("<tr>"));
          client.println(F("<th>Temperature</th>"));
          client.println(F("<th>Humidity</th>"));
          client.println(F("<th>Pressure</th>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("<tr>"));
            client.print(F("<td>"));
            client.print(payload[0].temp);
          client.println(F("&#176;</td>"));

            client.print(F("<td>"));            
            client.print(payload[0].humid);
          client.println(F("%</td>"));

            client.print(F("<td>"));
            client.print(payload[0].pres);    
          client.println(F(" in</td>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("</table>"));

          client.println(F("<br />"));
          client.print(F("Sent: "));            
          client.print(payload[0].tx_counter);
          client.print(F("&nbsp;&nbsp;/&nbsp;&nbsp;"));
          client.print(F("Received: "));
          client.println(rx_counter[0]);
          
          client.println(F("<br /><br />"));
          client.print(F("Battery Voltage: "));            
          client.print(payload[0].battvoltage);
          client.print(F("v"));            
          
          if (payload[0].battvoltage <= 2.67) {
            client.print(F("&nbsp;&nbsp;\<\<&nbsp;LOW!"));
          } 

          client.println(F(""));
          client.println(F("<br /><br />"));

          client.print(F("Last update: ")); 
          client.print(strdatetime[0]);
              
          client.println(F("<br /><br /><br />"));          


          // ****  GARAGE readings on web page   **** //
          
          client.println(F("<table>"));
          //----------------------------------------------
          client.println(F("<caption>My Garage</caption>"));
          client.println(F("<tr>"));
          client.println(F("<th>Temperature</th>"));
          client.println(F("<th>Humidity</th>"));
          client.println(F("<th>Pressure</th>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("<tr>"));
            client.print(F("<td>"));
            client.print(payload[1].temp);
          client.println(F("&#176;</td>"));

            client.print(F("<td>"));            
            client.print(payload[1].humid);
          client.println(F("%</td>"));

            client.print(F("<td>"));
          client.println(F("-----</td>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("</table>"));

          client.println(F("<br />"));
          client.print(F("Sent: "));            
          client.print(payload[1].tx_counter);
          client.print(F("&nbsp;&nbsp;/&nbsp;&nbsp;"));
          client.print(F("Received: "));
          client.println(rx_counter[1]);
          
          client.println(F("<br /><br />"));
          client.print(F("Battery Voltage: "));            
          client.print(payload[1].battvoltage);
          client.print(F("v"));            
          
          if (payload[1].battvoltage <= 2.67) {
            client.print(F("&nbsp;&nbsp;\<\<&nbsp;LOW!"));
          } 

          client.println(F(""));

          client.println(F("<br /><br />"));
          
          client.print(F("Last update: ")); 
          client.print(strdatetime[1]);
              
          client.println(F("<br /><br /><br />"));


          // ****   BASEMENT readings on Web Page   **** //

          client.println(F("<table>"));
          //----------------------------------------------
          client.println(F("<caption>My Basement</caption>"));
          client.println(F("<tr>"));
          client.println(F("<th>Temperature</th>"));
          client.println(F("<th>Humidity</th>"));
          client.println(F("<th>Pressure</th>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("<tr>"));
            client.print(F("<td>"));
            client.print(payload[2].temp);
          client.println(F("&#176;</td>"));

            client.print(F("<td>"));            
            client.print(payload[2].humid);
          client.println(F("%</td>"));

            client.print(F("<td>"));
          client.println(F("-----</td>"));
          client.println(F("</tr>"));
          //----------------------------------------------
          client.println(F("</table>"));

          client.println(F("<br />"));
          client.print(F("Sent: "));            
          client.print(payload[2].tx_counter);
          client.print(F("&nbsp;&nbsp;/&nbsp;&nbsp;"));
          client.print(F("Received: "));
          client.println(rx_counter[2]);
          
          client.println(F("<br /><br />"));
          client.print(F("Battery Voltage: "));            
          client.print(payload[2].battvoltage);
          client.print(F("v"));            
          
          if (payload[2].battvoltage <= 2.67) {
            client.print(F("&nbsp;&nbsp;\<\<&nbsp;LOW!"));
          } 

          client.println(F(""));
          client.println(F("<br /><br />"));

          client.print(F("Last update: ")); 
          client.print(strdatetime[2]);
              
          client.println(F("<br /><br /><br />"));          

          client.print(F("<i>"));
          client.print(F("Page refreshes automatically every "));
          client.print(WEBPAGE_REFRESH_MINS);
          client.print(F(" minutes")); 
          client.println(F("</i>"));          

          client.println(F("</body>"));

          client.println(F("</html>"));
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(5);

    // close the connection:
    client.stop();
    Serial.println(F("Disc"));

  }

  // Hey, the sender sent us a new payload - Yay!
  if (newpayload) {
    
    senderid = temppayload.sender_id;
    rx_counter[senderid]++;

    lcd.clear();   
    lcd.setCursor(0,0);

    switch (senderid) {
      case 0:
        lcd.print("Updating Porch");
        break;
      case 1:
        lcd.print("Updating Garage");
        break;
      case 2:
        lcd.print("Updating Basemnt");
        break;
    }
    
    showscreen = senderid;   //show display for sensor data just received
    
    payload[senderid].sender_id = temppayload.sender_id;
    payload[senderid].tx_counter = temppayload.tx_counter;
    payload[senderid].temp = temppayload.temp;
    payload[senderid].humid = temppayload.humid;
    payload[senderid].pres = temppayload.pres;
    payload[senderid].battvoltage = temppayload.battvoltage;
    payload[senderid].resetcount = temppayload.resetcount;

    printf("Sender ID = %d %d\n",temppayload.sender_id,payload[senderid].sender_id);
    delay(1000);
    // Sender has been restarted, so reset the rx_counter too
    if (payload[senderid].resetcount) {
      rx_counter[senderid] = 1;
    }
    
    // read RTC
    if (RTC.read(tm)) {
      //convert UTC to local time
      localtime = makeTime(tm);

      // break down values from RTC read
      rtcmonth = month(localtime);
      rtcday = day(localtime);
      rtcyear = year(localtime)-2000;
      rtchour = hour(localtime);
      rtcminute = minute(localtime);
      rtcsecond = second(localtime);

      // at 3am local time on 1st and 15th of each month, update date/time on RTC from NTP server
      // and put star on LCD's from 3am to 9am indicating update happened 
      if ((rtchour==3) && ((rtcday==1) || (rtcday==15)))                           {
        
        if (!timesetdone) {
         
          // Try to retreive date and time from NTP and set RTC
          SetTime();

          if(timeStatus()!= timeSet) 
            Serial.println("Unable to sync with the RTC");
          else
          {
            lcd.setCursor(0, 2);
            lcd.print(F("RTC active"));

            Serial.println("RTC active");
            Serial.print(rtcmonth);
            Serial.print("/");
            Serial.print(rtcday);
            Serial.print("/");
            Serial.print(rtcyear);
            Serial.print(" ");
            Serial.print(rtchour);
            Serial.print(":");
            Serial.print(rtcminute);
            Serial.print(":");
            Serial.println(rtcsecond);
      
            //Spew time stamp to LCD
            char strtmp[20];
            sprintf(strtmp, "%02d/%02d/%02d %02d:%02d:%02d",rtcmonth,rtcday,rtcyear,rtchour,rtcminute,rtcsecond);
            lcd.setCursor(0, 3);
            lcd.print(strtmp);
      
            timesetdone = true;
            printstar = true;
            
            delay(1000);
          }
        }
      } else {
        timesetdone = false;
      }
      
      // clear update star on LCD's at 9am
      if ((rtchour==9) && (printstar==true)) {
        printstar = false;
      }
      
    } else {
      if (RTC.chipPresent()) {
        Serial.println("The DS1307 is stopped.  Please run the SetTime");
        Serial.println("example to initialize the time and begin running.");
        Serial.println();
      } else {
        Serial.println("DS1307 read error!  Please check the circuitry.");
        Serial.println();
      }
      delay(3000);
    }
 
    // format var to hold nice-looking date and time for displaying to LCD 
    sprintf(strdatetime[senderid], "%02d/%02d/%02d %02d:%02d:%02d",rtcmonth,rtcday,rtcyear,rtchour,rtcminute,rtcsecond);
    sprintf(strdatetimexmit, "%02d/%02d %02d:%02d",rtcmonth,rtcday,rtchour,rtcminute);
    //strdatetimeshort[senderid] = strdatetimexmit;
    strncpy(strdatetimeshort[senderid], strdatetimexmit, 12);
    
    // Convert incoming sensor readings from float to string for printing
    // Set field widths to 0 allows it to expand as needed but no forced leading spaces
    dtostrf(payload[senderid].temp, 0, 1, strtemp[senderid]);
    dtostrf(payload[senderid].humid, 0, 1, strhumid[senderid]);
    dtostrf(payload[senderid].pres, 0, 2, strpres[senderid]);
    dtostrf(payload[senderid].battvoltage, 0, 2, strvolt[senderid]);
    dtostrf(payload[senderid].tx_counter, 0, 0, strsent[senderid]);
    dtostrf(rx_counter[senderid], 0, 0, strrecd[senderid]);
    dtostrf(printstar, 0, 0, strupdtime);
    dtostrf(senderid, 0, 0, strsenderid);

    memset(mystring[senderid], 0, sizeof mystring[senderid]);   // clear mystring
    strcat(mystring[senderid],strsenderid);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strtemp[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strhumid[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strpres[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strvolt[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strdatetimexmit);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strsent[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strrecd[senderid]);
    strcat(mystring[senderid],";");
    strcat(mystring[senderid],strupdtime);
    strcat(mystring[senderid],";");

     
    // Spew it to serial
    printf("Got payload from %i...%s (%i)\n\r",senderid,mystring[senderid],rx_counter[senderid]);


    // lcd stuff was here
    updatedisplay=true;

    newpayload=false;

    if (senderid==0) 
    { 
      // Set up datasteams for Xively
      datastreams_porch[0].setFloat(payload[senderid].temp);
      datastreams_porch[1].setFloat(payload[senderid].humid);
      datastreams_porch[2].setFloat(payload[senderid].pres);
      datastreams_porch[3].setFloat(payload[senderid].battvoltage);
      
      // Send to Xively
      Serial.print(F("x-up->"));
      EthernetClient xclient;
      XivelyClient xivelyclient(xclient);
      int ret = xivelyclient.put(feed_porch, xivelyKey);
      xclient.stop();
            
      //return code from xively (200 is good)
      printf("%i\n\r",ret);
    } 
    else if (senderid == 1) 
    {
      // Set up datasteams for Xively
      datastreams_garage[0].setFloat(payload[senderid].temp);
      datastreams_garage[1].setFloat(payload[senderid].humid);
      datastreams_garage[2].setFloat(payload[senderid].battvoltage);
      
      // Send to Xively
      Serial.print(F("x-up->"));
      EthernetClient xclient;
      XivelyClient xivelyclient(xclient);
      int ret = xivelyclient.put(feed_garage, xivelyKey);
      xclient.stop();
            
      //return code from xively (200 is good)
      printf("%i\n\r",ret);
    }
    else if (senderid == 2)
    { 
      // Set up datasteams for Xively
      datastreams_basement[0].setFloat(payload[senderid].temp);
      datastreams_basement[1].setFloat(payload[senderid].humid);
      datastreams_basement[2].setFloat(payload[senderid].battvoltage);
      
      // Send to Xively
      Serial.print(F("x-up->"));
      EthernetClient xclient;
      XivelyClient xivelyclient(xclient);
      int ret = xivelyclient.put(feed_basement, xivelyKey);
      xclient.stop();
            
      //return code from xively (200 is good)
      printf("%i\n\r",ret);
    } 
   
    // Send to remote server node
    delay(1000);
    EthernetClient remoteclient;
    Serial.println(F("Connecting..."));

    if (remoteclient.connect(remoteserver,7878)) {
      Serial.println(F("Connected"));
      remoteclient.println(mystring[senderid]);
    } else {
      Serial.println(F("Conn fail"));
    }
    
//    delay(1000);
//   
//    if (remoteclient.available()) {
//      char c = remoteclient.read();
//      Serial.print(F("Resp-->"));
//      Serial.print(c);
//    }

    delay(3000);

    if (!remoteclient.connected()) {
      Serial.println(F("Disc..."));
      remoteclient.stop();
      Serial.println(F("Disc."));
      Serial.println();
    }
     
    delay(1000);   // let things stabilize     

  }

}



void check_radio(void)
{
  // What happened?
  bool tx,fail,rx;
  radio.whatHappened(tx,fail,rx);

//  // Have we successfully transmitted?
//  if ( tx )
//  {
//      printf("Something is wrong...this is the receiver not the sender!!\n\r");
//  }
//
//  // Have we failed to transmit?
//  if ( fail )
//  {
//      printf("Payload Failure?\n\r");
//  }

  // Did we receive a message?
  if ( rx )
  {
        // Fetch payload
        radio.read(&temppayload, sizeof(temppayload));
        
        newpayload=true;   //set flag so work can be done in loop()
  }
}


void updatelcd(int p_sender)
{
  // Spew it to LCD
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(strtemp[p_sender]);
  lcd.print(char(223));

  lcd.setCursor(12, 0);
  lcd.print(strhumid[p_sender]);
  lcd.print(F("%"));

  lcd.setCursor(0, 1);
  switch (p_sender) {
    case 0:
      lcd.print(strpres[p_sender]);
      lcd.print(F("\""));
      break;
    default:
      lcd.print(F("----"));      
      break;
  }
  
  lcd.setCursor(12, 1);
  lcd.print(strvolt[p_sender]);
  lcd.print(F("v"));

  // alert if batt voltage < 2.67v
  lcd.setCursor(7, 1);
  if (payload[p_sender].battvoltage <= 2.67)
    lcd.print(F("LOW>"));
 
  //Last update date/time
  lcd.setCursor(0, 2);
  lcd.print(F("Last:"));

  lcd.setCursor(6, 2);
  lcd.print(strdatetimeshort[p_sender]);
  
  //Send/Recd packets
  lcd.setCursor(0, 3);
  lcd.print(F("S:"));

  lcd.setCursor(2, 3);
  lcd.print(strsent[p_sender]);

  lcd.setCursor(12, 3);
  lcd.print(F("R:"));

  lcd.setCursor(14, 3);
  lcd.print(strrecd[p_sender]);

 
  // print stars on LCD's from 3am to 9am indicating RTC was updated
  lcd.setCursor(19, 2);
  if (printstar)
    lcd.print("*");
  
  // print "P" indicating Porch location, "G" for Garage
  lcd.setCursor(19, 3);
  switch (p_sender) {
    case 0:
      lcd.print("P");
      break;
    case 1:
      lcd.print("G");
      break;
    case 2:
      lcd.print("B");
      break;
  }

}



int GET_NTP_TIME()
{
  int success=0;
    
  // send an NTP packet to a time server
  sendNTPpacket(timeServer); 

  // wait to see if a reply is available
  delay(1000);  

  if ( Udp.parsePacket() ) { 
   
    success=1;
   
    // We've received a packet, read the data from it
    Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  

    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;  

    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800
    const unsigned long seventyYears = 2208988800UL;     

    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;                                
    
  }
  else Serial.println("No NTP Response :-(");

  return success;
}



// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision

  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:            
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  Udp.endPacket();
}
void SetTime()
{
  
  lcd.clear();
  lcd.print(F("Setting clock..."));
  
  // Try to get the date and time
  int trys=0;
  int succeed=0;
  while((!succeed) && (trys<5)) {
    succeed = GET_NTP_TIME();
    trys++;
  }
  
  if (succeed) 
  {
    // Convert UTC to local time with daylight saving
    localtime = myTZ.toLocal(epoch, &tcr);
  
    // Set RTC to local time
    RTC.set(localtime);

    Serial.println("RTC set");
    lcd.setCursor(0, 1);
    lcd.print(F("RTC set"));
  } else {
    Serial.println("RTC set failed!");
    lcd.setCursor(0, 1);
    lcd.print(F("RTC set failed!"));
  }
  
}
