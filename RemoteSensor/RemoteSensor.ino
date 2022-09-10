#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Adafruit_AHT10.h>

const char* ssid = "Vachon";
const char* password = "51cypress";

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32

Adafruit_AHT10 aht;

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


void setup() {
  Serial.begin(9600);
  // put your setup code here, to run once:
  
  if(aht.begin()){
    //display.println("Temp OK");
    Serial.println("Temp OK");
  }else{
    //display.println("Temp FAIL");
    Serial.println("Temp FAIL");
  }
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
  display.clearDisplay();
  display.display();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);
  display.display();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  for(int i=0;i<120 && (WiFi.status() != WL_CONNECTED);i++){
    delay(500);
    display.print(".");
    display.display();
  }
  display.println("");
  display.print("IP: ");
  display.print(WiFi.localIP());
  display.display();
  delay(2000);
  display.setTextSize(3);
}

void loop() {
  // put your main code here, to run repeatedly:
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  float tempF = temp.temperature*9.0/5.0+32.0;
  display.setCursor(0, 0);
  display.clearDisplay();
  display.print(tempF,2);
  display.println("F");
  display.display();
  if(WiFi.status()== WL_CONNECTED){
      Serial.print("GET:");
      WiFiClient wiClient;
      HTTPClient http;
      String serverPath = "http://192.168.1.116/d?x="+String(tempF);
      if(http.begin(wiClient,serverPath.c_str())){
        int httpResponseCode = http.GET();
        http.end();
        Serial.println(httpResponseCode);
      }else{
        Serial.println("FAIL");
      }
  }else{
    Serial.println("Reconnecting...");
    WiFi.reconnect();
  }
  delay(10000);
}
