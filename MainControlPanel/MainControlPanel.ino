#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTP_Method.h>
#include <Uri.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_timer.h"



#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 128 // Change this to 96 for 1.27" OLED.

// You can use any (4 or) 5 pins 

#define DC_PIN   1
#define CS_PIN   5
#define RST_PIN  3
#define HEAT_RELAY_PIN 4
#define COOL_RELAY_PIN 2
#define BUTTON_PIN 15

#define BUTTON_COUNT_THRESH 4

#define HISTORY_DIV 6
#define ROT_A 16
#define ROT_B 17

// Color definitions
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0  
#define WHITE           0xFFFF

#define MIN_TEMP 60.0
#define MAX_TEMP 90.0

#define MODE_HEAT 0
#define MODE_COOL 1
#define MODE_OFF -1

#define LOCAL_TEMP 0
#define REMOTE_TEMP 1
#define REMOTE_TEMP_TIMEOUT 60000

#define MIN_CYCLE_TIME_MS 300000
#define MAX_CYCLE_ON_TIME_MS 3600000

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <SPI.h>
#include <Adafruit_AHT10.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

const char* ssid = "Vachon";
const char* password = "51cypress";

WebServer server(80);
Preferences prefs;

double tempF,remoteTemp;
int hvacMode=MODE_COOL;
bool hvacActive=false;
float hyster = 0.25;
volatile float setTemp=78.0;
int triggerCountThresh = 5;
int triggerCount = 0;
int tempSource = LOCAL_TEMP;

unsigned long lastRemoteTempSync=0;
unsigned long lastIncrement=0;
unsigned long lastCycleChange=-MIN_CYCLE_TIME_MS;
unsigned long lastU=5000;
volatile boolean updateScreen=false;
int16_t rect_x, rect_y;
uint16_t rect_w, rect_h, rect_h2;
String tempString="";
String humidString="";
String targetString="";
int historyDivide=0;

float tempHist[SCREEN_WIDTH];
boolean activeHist[SCREEN_WIDTH];
int tempHistIndex=0;

int buttonDownCount=0;
bool modeState=false;
float modeSwitchThresh = 5;

Adafruit_SSD1351 OLED = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, CS_PIN, DC_PIN, RST_PIN);
Adafruit_AHT10 aht;

unsigned long myMillis(){
  return (unsigned long)(esp_timer_get_time()/1000);
}

void IRAM_ATTR rotIttr(){
  if(myMillis()-lastIncrement>250){
    if(digitalRead(ROT_B)){
      setTemp-=1;
    }else{
      setTemp+=1;
    }
    lastIncrement=myMillis();
    updateScreen=true;
  }
}

const unsigned short rgb(unsigned char red, unsigned char green, unsigned char blue){
  return (red>>3)<<11|(green>>2)<<5|(blue>>3);
}

const unsigned short HueToColor(unsigned short hue){
  int r,g,b;
  float c;
  float seg=10922.6;
  hue=hue%65536;
  if(hue<seg){
    c=hue/(seg);
    r=255;
    g=c*255.0;
    b=0;
  }
  if(hue>=seg && hue<2*seg){
    c=(hue-seg)/(seg);
    r=255-c*255.0;
    g=255;
    b=0;
  }
  if(hue>=2*seg && hue<3*seg){
    c=(hue-2*seg)/(seg);
    r=0;
    g=255;
    b=c*255.0;
  }
  if(hue>=3*seg && hue<4*seg){
    c=(hue-3*seg)/(seg);
    r=0;
    g=255-c*255.0;
    b=255;
  }
  if(hue>=4*seg && hue<5*seg){
    c=(hue-4*seg)/(seg);
    r=c*255.0;
    g=0;
    b=255;
  }
  if(hue>=5*seg && hue<6*seg){
    c=(hue-5*seg)/(seg);
    r=255;
    g=0;
    b=255-c*255.0;
  }
  return rgb(r,g,b);
}

const unsigned short FtoColor(double f){
  if(f<MIN_TEMP){return BLUE;}
  if(f>MAX_TEMP){return RED;}
  unsigned short p = (unsigned short)((43690*(f-MIN_TEMP))/(MAX_TEMP-MIN_TEMP));
  return HueToColor(43960-p);
}

String htmlIndexPage = "<html><head><title>WifiOLED Thermostat</title><script>const baseURL='/';let hist=[];const maxHist=1000;const dataTimeout=10000;function getInfo(){fetch(`${baseURL}i`).then(response=>response.json()).then(data=>{hist.push({...data,datestamp:Math.floor(Date.now()/dataTimeout)*dataTimeout});while(hist.length>maxHist){hist.shift();}renderHist();renderLive();setTimeout(getInfo,dataTimeout);localStorage.setItem('hist',JSON.stringify(hist));}).catch((e)=>{console.error(e);setTimeout(getInfo,dataTimeout);});}function renderHist(){const data=hist[hist.length-1];const ctx=canv.getContext('2d');const{width,height}=canv;const graphRect={top:height*0,bottom:height*1,left:0,right:width,height:height*1.00,width};const maxTemp=hist.reduce((acc,{temp})=>{if(temp>acc){return temp;}return acc},data.setTemp+0.51)+0.5;const minTemp=hist.reduce((acc,{temp})=>{if(temp<acc){return temp;}return acc},data.setTemp-0.51)-0.5;const minTime=hist[0].datestamp;const BGGrad=ctx.createLinearGradient(graphRect.left,graphRect.top,graphRect.left,graphRect.bottom);BGGrad.addColorStop(1-(data.setTemp+1-minTemp)/(maxTemp-minTemp),'hsl(0deg 100% 95%)');BGGrad.addColorStop(1-(data.setTemp-minTemp)/(maxTemp-minTemp),'hsl(120deg 100% 95%)');BGGrad.addColorStop(1-(data.setTemp-1-minTemp)/(maxTemp-minTemp),'hsl(240deg 100% 95%)');ctx.fillStyle=BGGrad;ctx.fillRect(graphRect.left,graphRect.top,graphRect.width,graphRect.height);const LineGrad=ctx.createLinearGradient(graphRect.left,graphRect.top,graphRect.left,graphRect.bottom);LineGrad.addColorStop(1-(data.setTemp+1-minTemp)/(maxTemp-minTemp),'hsl(0deg 50% 50%)');LineGrad.addColorStop(1-(data.setTemp-minTemp)/(maxTemp-minTemp),'hsl(120deg 50% 50%)');LineGrad.addColorStop(1-(data.setTemp-1-minTemp)/(maxTemp-minTemp),'hsl(240deg 50% 50%)');ctx.strokeStyle=LineGrad;ctx.fillStyle=LineGrad;ctx.beginPath();ctx.moveTo(graphRect.right-(Date.now()-minTime)/dataTimeout,graphRect.bottom);for(i=0;i<hist.length;i++){const x=graphRect.right-(Date.now()-hist[i].datestamp)/dataTimeout;const y=graphRect.bottom - graphRect.height*(hist[i].temp-minTemp)/(maxTemp-minTemp);ctx.lineTo(x,y);}ctx.lineTo(graphRect.right,graphRect.bottom);ctx.lineWidth=1;ctx.stroke();ctx.fill();ctx.fillStyle='#FFFFFF';for(i=1;i<hist.length;i++){const x=graphRect.right-(Date.now()-hist[i-1].datestamp)/dataTimeout;const x1=graphRect.right-(Date.now()-hist[i].datestamp)/dataTimeout;if(hist[i].active){ctx.fillRect(x-1,graphRect.bottom-5,x1-x+1,5);}}ctx.fillStyle=LineGrad;ctx.font='0.75cm sans-serif';let y=graphRect.bottom - graphRect.height*(data.setTemp-minTemp)/(maxTemp-minTemp);ctx.beginPath();ctx.moveTo(graphRect.left,y);ctx.lineTo(graphRect.right,y);ctx.strokeStyle='#000000';ctx.lineWidth=1.5;ctx.stroke();ctx.fillStyle='#000000';ctx.fillText(data.setTemp,graphRect.left,y-2);y=graphRect.bottom - graphRect.height*(data.setTemp+0.5-minTemp)/(maxTemp-minTemp);ctx.beginPath();ctx.moveTo(graphRect.left,y);ctx.lineTo(graphRect.right,y);ctx.strokeStyle='#FF0000';ctx.stroke();ctx.fillText(data.setTemp+0.5,graphRect.left,y-2);y=graphRect.bottom - graphRect.height*(data.setTemp-0.5-minTemp)/(maxTemp-minTemp);ctx.beginPath();ctx.moveTo(graphRect.left,y);ctx.lineTo(graphRect.right,y);ctx.strokeStyle='#0000FF';ctx.stroke();ctx.fillText(data.setTemp-0.5,graphRect.left,y-2);y=graphRect.bottom - graphRect.height*(data.temp-minTemp)/(maxTemp-minTemp);ctx.beginPath();ctx.moveTo(graphRect.left,y);ctx.lineTo(graphRect.right,y);ctx.strokeStyle='#444444';ctx.stroke();ctx.fillText(data.temp,graphRect.left,y-2);ctx.font='0.75cm sans-serif';ctx.strokeStyle='#444444';ctx.lineWidth=1.5;for(i=0;i<hist.length;i++){if(!(hist[i].datestamp%(dataTimeout*180))){const x=graphRect.right-(Date.now()-hist[i].datestamp)/dataTimeout;ctx.beginPath();ctx.moveTo(x,graphRect.bottom);ctx.lineTo(x,graphRect.top);ctx.stroke();ctx.fillText(new Date(hist[i].datestamp).toLocaleTimeString(),x+2,graphRect.top+24);}}}function renderLive(){const data=hist[hist.length-1];Object.keys(data).forEach(key=>{if(document.getElementById(`stat_${key}`)){document.getElementById(`stat_${key}`).innerHTML=prettyData(data,key);}if(document.getElementById(`set_${key}`)){document.getElementById(`set_${key}`).value=data[key];}});}function prettyData(data,key){if(key==='mode'){return{'-1':'Off','0':'Heat','1':'Cool'}[data[`${key}`]];}if(key==='lastCycleChange'){return Math.floor(data[key]/6000)/10;}if(key==='active'){return{'0':'False','1':'True'}[data[`${key}`]];}return data[key];}function setVal(vari,val){fetch(`${baseURL}${vari}?x=${val}`);}</script><style>*{font-family: sans-serif;font-size: 1cm;}#wrap{max-width: 1400px;margin: auto;}.statrow{display: flex;justify-content: space-around}.statblock{text-align: center;}.statlabel{font-weight: bold}.statvalue{font-variant: italic;display: inline;}input{width: 3em}#canv{width: 100%}</style></head><body><div id='wrap'><div class='statrow'><div class='statblock'><div class='statlabel'>Temp</div><div id='stat_temp' class='statvalue'> </div>F</div><div class='statblock'><div class='statlabel'>Last Change</div><div id='stat_lastCycleChange' class='statvalue'> </div>min</div><div class='statblock'><div class='statlabel'>Active</div><div id='stat_active' class='statvalue'> </div></div></div><canvas id='canv' width='1000' height='500'></canvas><div class='statrow'><div class='statblock'><div class='statlabel'>Set Temp</div><input id='set_setTemp'/> F<button onclick='setVal(`t`,set_setTemp.value)'>Save</button></div><div class='statblock'><div class='statlabel'>Set Mode</div><select id='set_mode'><option value='0'>Heat</option><option value='1'>Cool</option><option value='-1'>Off</option></select><button onclick='setVal(`m`,set_mode.value)'>Save</button></div><div class='statblock'><div class='statlabel'>Set Source</div><select id='set_source'><option value='0'>Local</option><option value='1'>Remote</option></select><button onclick='setVal(`s`,set_source.value)'>Save</button></div></div></div></body><script>hist=JSON.parse(localStorage.getItem('hist','[]'));if(!hist){hist=[];}getInfo();</script></html>";


void handleRoot(){
  server.send(200,"text/html",htmlIndexPage.c_str());
}
void handleDowntemp(){
  float x = server.arg("x").toFloat();
  remoteTemp = x;
  lastRemoteTempSync=myMillis();
  handleGetInfo();
}
void handleSetSource(){
  int x = server.arg("x").toInt();  
  tempSource=x;
  prefs.putInt("tempSource",tempSource);
  handleGetInfo();
}
void handleSetTemp(){
  float x = server.arg("x").toFloat();
  setTemp = x;
  prefs.putFloat("setTemp",setTemp);
  handleGetInfo();
}
void handleHyster(){
  float x = server.arg("x").toFloat();
  hyster = x;
  prefs.putFloat("hyster",hyster);
  handleGetInfo();
}
void setHvacActive(boolean x){
  if(x!=hvacActive){
    lastCycleChange=myMillis();
    triggerCount=0;
  }
  hvacActive=x;
}

void handleSetMode(){
  int x = server.arg("x").toInt();  
  hvacMode=x;
  prefs.putInt("hvacMode",hvacMode);
  setHvacActive(false);
  handleGetInfo();
}
void handleGetInfo(){
  String s = "{\"temp\":"+String(tempF,2)+","+
    "\"setTemp\":"+String(setTemp,2)+","+
    "\"mode\":"+String(hvacMode)+","+
    "\"active\":"+String(hvacActive)+","+
    "\"source\":"+String(tempSource)+","+
    "\"remoteTemp\":"+String(remoteTemp)+","+
    "\"lastRemoteTempSync\":"+String(myMillis()-lastRemoteTempSync)+","+
    "\"lastCycleChange\":"+String(myMillis()-lastCycleChange)+","+
    "\"triggerCount\":"+String(triggerCount)+","+
    "\"hyster\":"+String(hyster)+
    "}";
  server.send(200,"text/json",s.c_str());
}

void setup(void) {
  prefs.begin("therm",false);
  pinMode(ROT_A,INPUT_PULLUP);
  pinMode(ROT_B,INPUT_PULLUP);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  pinMode(HEAT_RELAY_PIN,OUTPUT);
  digitalWrite(HEAT_RELAY_PIN,false);
  pinMode(COOL_RELAY_PIN,OUTPUT);
  digitalWrite(COOL_RELAY_PIN,false);
  OLED.begin();
  OLED.fillRect(0, 0, 128, 128, BLACK);
  OLED.setTextColor(WHITE);
  OLED.setTextSize(1);
  OLED.setCursor(1,1);
  OLED.print("Thermometer...");
  if(aht.begin()){
    OLED.println("OK");
  }else{
    OLED.println("FAIL");
  }
  OLED.print("Wifi: ");
  OLED.print(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  for(int i=0;i<120 && (WiFi.status() != WL_CONNECTED);i++){
    delay(500);
    OLED.print(".");
  }
  
  OLED.print("IP: ");
  OLED.println(WiFi.localIP());
  if (MDNS.begin("THERM")) {
    OLED.println("THERM.local");
  }
  setTemp=prefs.getFloat("setTemp",78.0);
  hvacMode=prefs.getInt("hvacMode",MODE_OFF);
  tempSource=prefs.getInt("tempSource",LOCAL_TEMP);
  hyster=prefs.getFloat("hyster",0.25);
  server.on("/", handleRoot);
  server.on("/d", handleDowntemp);
  server.on("/s", handleSetSource);
  server.on("/t", handleSetTemp);
  server.on("/m", handleSetMode);
  server.on("/h", handleHyster);
  server.on("/i", handleGetInfo);
  server.begin();
  htmlIndexPage.replace("REMOTE_TEMP_TIMEOUT",String(REMOTE_TEMP_TIMEOUT));
  htmlIndexPage.replace("MIN_CYCLE_TIME_MS",String(MIN_CYCLE_TIME_MS));
  htmlIndexPage.replace("TRIGGER_COUNT_THRESH",String(triggerCountThresh));
  delay(3000);
  for(int i=0;i<SCREEN_WIDTH;i++){
    tempHist[i]=-1000;
    OLED.fillRect(i,0,1,SCREEN_HEIGHT/5,rgb(i*2,0,0));
    OLED.fillRect(i,SCREEN_HEIGHT/5,1,2*SCREEN_HEIGHT/5,rgb(0,i*2,0));
    OLED.fillRect(i,2*SCREEN_HEIGHT/5,1,3*SCREEN_HEIGHT/5,rgb(0,0,i*2));
    OLED.fillRect(i,3*SCREEN_HEIGHT/5,1,4*SCREEN_HEIGHT/5,FtoColor(60+i/4));
    OLED.fillRect(i,4*SCREEN_HEIGHT/5,1,5*SCREEN_HEIGHT/5,HueToColor(i*512));
  }
  delay(1000);
  OLED.fillRect(0, 0, 128, 128, BLACK);
  attachInterrupt(ROT_A,rotIttr,FALLING);
}

void drawHist(){
  
  float minTemp=999;
  float maxTemp=-999;
  for(int i=0;i<SCREEN_WIDTH;i++){
    if(tempHist[i]==-1000){continue;}
    if(tempHist[i]>maxTemp){maxTemp=tempHist[i];}
    if(tempHist[i]<minTemp){minTemp=tempHist[i];}
  }
  if(maxTemp-minTemp<0.2){
    maxTemp+=0.1;
    minTemp-=0.1;
  }
  for(int x=0;x<SCREEN_WIDTH-1;x++){
    int i=(x+tempHistIndex)%SCREEN_WIDTH;
    int i2=(x+1+tempHistIndex)%SCREEN_WIDTH;
    if(tempHist[i]==-1000){continue;}
    int y=(int)(32.0*(tempHist[i]-minTemp)/(maxTemp-minTemp));
    int y2=(int)(32.0*(tempHist[i2]-minTemp)/(maxTemp-minTemp));
    if(y>32){y=32;}
    if(y<0){y=0;}
    if(y2>32){y2=32;}
    if(y2<0){y2=0;}
    y=SCREEN_HEIGHT-y;
    y2=SCREEN_HEIGHT-y2;
    if(!x){OLED.drawLine(x,y,x+1,y2,HueToColor(43960-43690*(tempHist[i]-minTemp)/(maxTemp-minTemp)));}
    OLED.drawLine(x+1,y2,x+1,SCREEN_HEIGHT,HueToColor(43960-43690*(tempHist[i]-minTemp)/(maxTemp-minTemp)));
    if(activeHist[i2]){
      OLED.drawPixel(x+1,SCREEN_HEIGHT-1,WHITE);
    }
  }
  
}



void hvacLogic(){
  if(myMillis()-lastU>5000){
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
    tempF = temp.temperature*9.0/5.0+32.0;
    if(tempSource==REMOTE_TEMP && myMillis()-lastRemoteTempSync < REMOTE_TEMP_TIMEOUT){
      tempF = remoteTemp;
    }else{
      tempSource=LOCAL_TEMP;
    }
    
    if(myMillis()-lastCycleChange>MIN_CYCLE_TIME_MS){  //If it's been X time since the last hvac state change
      if(hvacActive){ //If we're active
        if(hvacMode==MODE_COOL){ //And we're cooling
          if(tempF<setTemp-hyster){ //Check if we're too cold
            triggerCount++; //If so count up
            if(triggerCount>triggerCountThresh){ //If the trigger count is above the threshold, we're definitely too cold
              hvacActive=false;//Turn off the AC
              triggerCount=0;//Reset the trigger
              lastCycleChange=myMillis();//State change so update this
            }
          }
        }
        if(hvacMode==MODE_HEAT){//If we're heating
          if(tempF>setTemp+hyster){//and it seems too hot
            triggerCount++;//Count
            if(triggerCount>triggerCountThresh){//If count is good, we're def too hot
              hvacActive=false;//Kill heat
              triggerCount=0;//reset count
              lastCycleChange=myMillis();//state change
            }
          }
        }
        if(hvacMode==MODE_OFF){//If we're off
          hvacActive=false;//Kill ??
          triggerCount=0;//reset count
          lastCycleChange=myMillis();//state change
        }
      }else{//HVAC is off
        if(hvacMode==MODE_COOL){//In cool mode
          if(tempF>setTemp+hyster){//Is it too hot?
            triggerCount++;//count
            if(triggerCount>triggerCountThresh){//yep too hot
              hvacActive=true;//turn on ac
              triggerCount=0;
              lastCycleChange=myMillis();
            }
          }
        }
        if(hvacMode==MODE_HEAT){//in heat mode
          if(tempF<setTemp-hyster){//is it too cold
            triggerCount++;//count
            if(triggerCount>triggerCountThresh){//yep too cold
              hvacActive=true;//heat
              triggerCount=0;
              lastCycleChange=myMillis();
            }
          }
        }
      }
    }

    //Max runtime timer and trip
    if(hvacActive && myMillis()-lastCycleChange>MAX_CYCLE_ON_TIME_MS){
      hvacActive=false;
      lastCycleChange=myMillis();
      triggerCount=0;
    }
    //Protect against ESD induced events
    float oSetTemp = setTemp;
    if(hvacMode==MODE_HEAT){
      digitalWrite(COOL_RELAY_PIN,false);
      digitalWrite(HEAT_RELAY_PIN,hvacActive);
    }
    if(hvacMode==MODE_COOL){
      digitalWrite(COOL_RELAY_PIN,hvacActive);
      digitalWrite(HEAT_RELAY_PIN,false);
    }
    if(hvacMode==MODE_OFF){
      digitalWrite(COOL_RELAY_PIN,false);
      digitalWrite(HEAT_RELAY_PIN,false);
    }
    delay(10);
    setTemp=oSetTemp;
    
    if(historyDivide==0){
      tempHist[tempHistIndex]=tempF;
      activeHist[tempHistIndex]=hvacActive;
      tempHistIndex=(tempHistIndex+1)%SCREEN_WIDTH;
      if (WiFi.status() != WL_CONNECTED){
        WiFi.disconnect();
        WiFi.reconnect();
      }
    }
    historyDivide=(historyDivide+1)%HISTORY_DIV;
    
    tempString = String(tempF,0);
    humidString = String(humidity.relative_humidity,0)+"%";
    updateScreen=true;
    lastU=myMillis();
  }
}

void handleButton(){
  if(!digitalRead(BUTTON_PIN)){
    buttonDownCount++;
    if(buttonDownCount>=BUTTON_COUNT_THRESH){
      updateScreen=true;
      buttonDownCount=0;
      if(hvacMode==MODE_HEAT){
        hvacMode=MODE_OFF;
        if(hvacActive){
          lastCycleChange=myMillis();
        }
        hvacActive=false;
        triggerCount=0;        
        modeState=false;
      }else{
        if(hvacMode==MODE_COOL){
          hvacMode=MODE_OFF;
          if(hvacActive){
            lastCycleChange=myMillis();
          }
          hvacActive=false;
          triggerCount=0;
          modeState=true;
        }else{
          if(hvacMode==MODE_OFF){
            if(modeState){
              hvacMode=MODE_HEAT;
            }else{
              hvacMode=MODE_COOL;
            }
          }
        }
      }
      prefs.putInt("hvacMode",hvacMode);
    }
  }else{
    buttonDownCount=0;
  }
}

void updateDisplay(){
    if(updateScreen){
      prefs.putFloat("setTemp",setTemp);
      updateScreen=false;
      targetString = "";
      
      OLED.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK);
      OLED.setTextColor(FtoColor(tempF));
      OLED.setFont(&FreeSansBold24pt7b);
      OLED.setTextSize(2);
      OLED.getTextBounds(tempString,0,0,&rect_x, &rect_y,&rect_w,&rect_h);
      OLED.setCursor(SCREEN_WIDTH/2-rect_w/2,96);
      OLED.println(tempString);
      
      if(hvacMode==MODE_COOL){
        targetString="Cool: "+String(setTemp,0)+"F";
        if(hvacActive){OLED.setTextColor(BLUE);}
        else{OLED.setTextColor(rgb(0,0,64));}
      }
      if(hvacMode==MODE_HEAT){
        targetString="Heat: "+String(setTemp,0)+"F";
        if(hvacActive){OLED.setTextColor(RED);}
        else{OLED.setTextColor(rgb(64,0,0));}
      }
      if(hvacMode==MODE_OFF){
        targetString="Off";
        OLED.setTextColor(WHITE);
      }
      OLED.setFont(&FreeSansBold12pt7b);
      OLED.setTextSize(1);
      OLED.getTextBounds(targetString,0,0,&rect_x, &rect_y,&rect_w,&rect_h2);
      OLED.setCursor(SCREEN_WIDTH/2-rect_w/2,24);
      OLED.println(targetString);
      
      drawHist();
      if(tempSource==LOCAL_TEMP){
        OLED.setFont();
        OLED.setCursor(1,1);
        OLED.setTextColor(WHITE);
        OLED.print("L");
      }
      if(tempSource==REMOTE_TEMP){
        OLED.setFont();
        OLED.setCursor(1,1);
        OLED.setTextColor(RED);
        OLED.print("R");
      }
      
    }
  }
void loop() {
  server.handleClient();  
  hvacLogic();
  handleButton();
  updateDisplay();
  delay(250);
}
