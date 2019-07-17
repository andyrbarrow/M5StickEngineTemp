/* A program for M5Stick-C to collect data from three 1-wire temperature sensors and send them by
UDP to a SignalK server (Openplotter).

By Andy Barrow
GPL License applies
*/

#include <M5StickC.h>
#include <OneWire.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

OneWire  ds(26);  // on pin 26 (a 4.7K resistor is necessary)
WiFiUDP udp;

const char* ssid = "openplotter";
const char* password = "margaritaville";
IPAddress sigkserverip(10,10,10,1);
uint16_t sigkserverport = 55561;
byte sendSig_Flag = 1;
byte sensorCount = 1;

// Addresses of 3 DS18B20s and names
uint8_t sensor1[8] = { 0x28, 0xFF, 0x68, 0xAA, 0x85, 0x16, 0x04, 0xA5 };
uint8_t sensor2[8] = { 0x28, 0xFF, 0xB1, 0x5F, 0x85, 0x16, 0x03, 0x7B };
uint8_t sensor3[8] = { 0x28, 0xFF, 0xF7, 0x1D, 0x82, 0x17, 0x04, 0xD4 };
char* sensor1Key = "propulsion.main.coolantTemperature";
char* sensor2Key = "propulsion.main.exhaustTemperature";
char* sensor3Key = "propulsion.main.oilTemperature";
char* sDisplay1 = "Coolant";
char* sDisplay2 = "Exhaust";
char* sDisplay3 = "Oil";

// Function delcarations
void setup_wifi();
void clearscreen();
void testUDP();
void sendSigK(String sigKey, float data);

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting ");
  WiFi.begin(ssid, password);
  int reset_index = 0;
  clearscreen();
  M5.Lcd.println(" WiFi Connect");
  M5.Lcd.print(" ");
  M5.Lcd.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    M5.Lcd.print(".");
    //If WiFi doesn't connect in 30 seconds, do a software reset
    reset_index ++;
    if (reset_index > 60) {
      Serial.println("WIFI Failed - restarting");
      clearscreen();
      M5.Lcd.println(" WiFi Failed");
      M5.Lcd.println(" Restarting");
      delay(1000);
      ESP.restart();
    }
    if (WiFi.status() == WL_CONNECTED) {
      clearscreen();
      M5.Lcd.println(" WiFi");
      M5.Lcd.println(" Connected");
      M5.Lcd.println(WiFi.localIP());
      delay(500);
    }
  }
}

void clearscreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE ,BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0,0);
}

//      send signalk data over UDP - thanks to PaddyB!
void sendSigK(String sigKey, float data)
{
 if (sendSig_Flag == 1)
 {
   DynamicJsonBuffer jsonBuffer;
   String deltaText;

   //  build delta message
   JsonObject &delta = jsonBuffer.createObject();

   //updated array
   JsonArray &updatesArr = delta.createNestedArray("updates");
   JsonObject &thisUpdate = updatesArr.createNestedObject();   //Json Object nested inside delta [...
   JsonArray &values = thisUpdate.createNestedArray("values"); // Values array nested in delta[ values....
   JsonObject &thisValue = values.createNestedObject();
   thisValue["path"] = sigKey;
   thisValue["value"] = data;
   thisUpdate["Source"] = "EngineSensors";

   // Send UDP packet
   udp.beginPacket(sigkserverip, sigkserverport);
   delta.printTo(udp);
   udp.println();
   udp.endPacket();
   delta.printTo(Serial);
   Serial.println();
 }
}

void setup(void) {
  M5.begin();
  Serial.begin(9600);
  M5.Lcd.setRotation(1);
  setup_wifi();
}

void loop(void) {
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;
  char* sigKkeyName;
  char* sDisplayName;

  Serial.println();
  // if the WiFi has been lost, restart it
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  // rotate between the three sensors
  switch (sensorCount) {
    case 1:
      memcpy(addr, sensor1, 8);
      sigKkeyName = sensor1Key;
      sDisplayName = sDisplay1;
      sensorCount = 2;
      break;
    case 2:
      memcpy(addr, sensor2, 8);
      sigKkeyName = sensor2Key;
      sDisplayName = sDisplay2;
      sensorCount = 3;
      break;
    case 3:
      memcpy(addr, sensor3, 8);
      sigKkeyName = sensor3Key;
      sDisplayName = sDisplay3;
      sensorCount = 1;
      break;
    default:
      break;
  }
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end

  delay(1000);     // maybe 750ms is enough, maybe not

  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  byte cfg = (data[4] & 0x60);
  // at lower res, the low bits are undefined, so let's zero them
  if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
  else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
  else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
  //// default is 12 bit resolution, 750 ms conversion time

  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");
  clearscreen();
  M5.Lcd.println(" Temperature");
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("");
  M5.Lcd.setTextSize(2);
  M5.Lcd.print(" ");
  M5.Lcd.setTextSize(3);
  M5.Lcd.print (fahrenheit);
  M5.Lcd.println("F");
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("");
  M5.Lcd.setTextSize(2);
  M5.Lcd.print(" ");
  M5.Lcd.println(sDisplayName);
  sendSigK(sigKkeyName, (celsius+273.15)); //SignalK uses Kelvin

}
