#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>

LiquidCrystal_I2C lcd(0x3F,20,4); //Addr: 0x3F, 20 chars & 4 lines


byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE }; //unique physical mac address of your ethernet shield
IPAddress ip(192,168,1,78); // IP address dependent upon your network addresses.

// Initialize the Ethernet server library
// with the IP address and port you want to use
EthernetServer server(7878); //server port

String readString; 
String strsenderid = "-";
String strtemp[3] = "--";
String strhumid[3] = "--";
String strpres[3] = "--";
String strvolt[3] = "--";
String strdatetime[3] = "--";
String strsent[3] = "--";
String strrecd[3] = "--";
String strupdtime = "-";

int senderindex=0;
int tempindex=0;
int humidindex=0;
int pressindex=0;
int voltindex=0;
int datetimeindex=0;
int sentindex=0;
int recdindex=0;
int updtimeindex=0;

int senderid = 0;

boolean first = true;  //flag used so "Receiving..." text is only updated/shown during am actual receive, not continuously on every loop() iteration

boolean updatedisplay = false;   //force display to update?
byte showscreen = 0;

unsigned long Timer;  //"ALWAYS use unsigned long for timers, not int"
unsigned long timedelay = 30000;  //how often to change display between screens
unsigned long timepassed = 0;   


void setup(){

  //enable serial data print 
  Serial.begin(9600); 
  Serial.println(F("Waiting for client..."));
    
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Waiting for client");
  
  //start Ethernet
  Ethernet.begin(mac, ip);
  server.begin();

  Timer = millis();

}


void loop(){
  
  // Create a client connection
  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {

      if (first) {
        Serial.println(F("Receiving..."));
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Receiving..."));
        first = false;
      }
      
      delay(5);   //keeps web server from hanging
      if (client.available()) {
        char c = client.read();

        //read char by char HTTP request
        if (readString.length() < 100) {

          //store characters to string 
          readString += c; 
        } 

        //if HTTP request has ended
        if (c == '\n') {
          //readString = readString.replace("\r","");
          //readString = readString.replace("\n","");
          delay(1000);
          Serial.println(readString);

          //now output response 
//          client.println("+");
//          client.println();

          delay(50);
          client.stop();  //stop client
          Serial.println(F("Done"));

          //break apart semicolon-delmited string of values received from Receiver node
          senderindex = readString.indexOf(';');
          strsenderid = readString.substring(0,senderindex);
          senderid = strsenderid.toInt();
          
          tempindex = readString.indexOf(';', senderindex+1);
          strtemp[senderid] = readString.substring(senderindex+1,tempindex);
       
          humidindex = readString.indexOf(';', tempindex+1);
          strhumid[senderid] = readString.substring(tempindex+1,humidindex);
          
          pressindex = readString.indexOf(';', humidindex+1);
          strpres[senderid] = readString.substring(humidindex+1,pressindex);

          voltindex = readString.indexOf(';', pressindex+1);
          strvolt[senderid] = readString.substring(pressindex+1,voltindex);

          datetimeindex = readString.indexOf(';', voltindex+1);
          strdatetime[senderid] = readString.substring(voltindex+1,datetimeindex);

          sentindex = readString.indexOf(';', datetimeindex+1);
          strsent[senderid] = readString.substring(datetimeindex+1,sentindex);
          
          recdindex = readString.indexOf(';', sentindex+1);
          strrecd[senderid] = readString.substring(sentindex+1,recdindex);

          updtimeindex = readString.indexOf(';', recdindex+1);
          strupdtime = readString.substring(recdindex+1,updtimeindex);

          updatedisplay = true;

          readString="";  //clear string for next read
          first = true;

        }
      }
    }
  }
  
  
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
} 


void updatelcd(int p_sender)
{
  // Spew values to LCD
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
  
  //alert of low batt on 2.67v or lower
  lcd.setCursor(7, 1);
  if (strvolt[p_sender].toFloat() <= 2.67)
     lcd.print(F("LOW>"));

  lcd.setCursor(0, 2);
  lcd.print(F("Last:"));

  lcd.setCursor(6, 2);
  lcd.print(strdatetime[p_sender]);
  
  lcd.setCursor(0, 3);
  lcd.print(F("S:"));

  lcd.setCursor(2, 3);
  lcd.print(strsent[p_sender]);

  lcd.setCursor(12, 3);
  lcd.print(F("R:"));

  lcd.setCursor(14, 3);
  lcd.print(strrecd[p_sender]);

  lcd.setCursor(19, 2);
  if (strupdtime=="1")
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

