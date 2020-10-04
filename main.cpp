#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <SimpleTimer.h>
#include <ArduinoJson.h>
// #include <Fonts/FreeMonoBold9pt7b.h>

#define wifi_ssid "wifi"
#define wifi_password "pass"

#define mqtt_server "192.168.1.11"
#define mqtt_port 1883

//this will append a 6 digit unique code embeded in the ESP chip (so should be unique :)
//String mqtt_client_name = "espSensor"+String(ESP.getChipId());  
String mqtt_client_name = "espClockDisplay01";  // also need to change ini file for OTA if used

#define mqtt_user "NotUsedYet"
#define mqtt_password ""

#define LED 2

String in_topic = String("iot/" + mqtt_client_name + "/cmd/#");
String willTopic = String("iot/" + mqtt_client_name + "/LWT");
String out_topic_status = String("iot/" + mqtt_client_name + "/inform");
String out_topic = String("iot/" + mqtt_client_name + "/result");
String out_topic_light = String("iot/" + mqtt_client_name + "/analog");

#define MQTT_SEND_TIME "iot/sendtime"

#define PIN D3
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32, 8, PIN,
  NEO_MATRIX_BOTTOM     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB            + NEO_KHZ800
);

//180 rotate
// Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32, 8, PIN,
//   NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
//   NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
//   NEO_GRB            + NEO_KHZ800);

// Set Defaults
String dispText;
String dispColor;
int disp = 0;
int dispBrightness = 25;
int pixelsInText;
int x = matrix.width();
const uint16_t colors[] = {
  matrix.Color(255, 0, 0),
  matrix.Color(0, 255, 0),
  matrix.Color(0, 0, 255)
};

uint16_t hLineColour = matrix.Color(200,0,0);
uint16_t textColour = matrix.Color(80,200,0);
int displayText = 0; // 0 = time, 1 = scroll, 2 = static
unsigned long textDuration = 30 * 1000; // X seconds
SimpleTimer displayTextTimer;
int displayTextTimerId;
String messageScroll;
int maxTextDisplacement;
const int textPixelSize = 6;

const int displayBrightnessMap[] = {5, 25, 50, 100};
// higher light readings, lower the light level is actualy.
// DLEVEL to control what maped above is returned
#define DLEVEL_HIGH 200
#define DLEVEL_MID 400
#define DLEVEL_LOW 700
bool displayBrightnessAuto = false;

// // Define NTP properties
// #define NTP_OFFSET   60 * 60      // In seconds
// #define NTP_INTERVAL 60 * 1000    // In miliseconds
// #define NTP_ADDRESS  "10.2.1.1"  // change this to whatever pool is closest (see ntp.org)
String date;
String t;
const char * days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"} ;
const char * months[] = {"Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sep", "Oct", "Nov", "Dec"} ;
const char * ampm[] = {"AM", "PM"} ;

// // Set up the NTP UDP client
// WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

WiFiClient espClient;
PubSubClient client;

const unsigned long do_readings_time = 6 * 1000;  // X * seconds (1000=1s)
unsigned long readings_last_checked = 0;

const unsigned long update_time = 5 * 60 * 1000;  // X * minute (60000=1m)
unsigned long last_update_time = 0;

SimpleTimer timer;
void displayTime();
int analogLight(int);

void displayTextRun() {
  Serial.println("Display Text Timer Executed");
  displayText = 0;
  client.publish(out_topic.c_str(), "{\"result\": \"display-done\"}");
  displayTime();
}

void syncTime(time_t utc) {
  time_t local;
  TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //UTC - 4 hours - change this as needed
  TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //UTC - 5 hours - change this as needed
  Timezone usEastern(usEDT, usEST);
  local = usEastern.toLocal(utc);

  setTime(local);
}

void flashDisplay(int num) {
  Serial.print("F");
  int b = matrix.getBrightness();
  for(int i=0; i < num; i++) {
    delay(50);
    matrix.setBrightness(1);
    matrix.show();
    delay(50);
    matrix.setBrightness(b);
    matrix.show();
  }
}

int xp = matrix.width();
int pass = 0;
int currentDisplayBrightness = 0;
void doDisplayTextMessage() {
  //matrix.fillScreen(0);
  matrix.fillRect(0,0,32,7,0); //should clear all but bottom
  matrix.setCursor(xp, 0);
  matrix.print(messageScroll.c_str());
  if(++pass >=8 ) { //half width
    pass = 0;
    currentDisplayBrightness = matrix.getBrightness();
    matrix.setBrightness(1);
  } else if(currentDisplayBrightness > 1) {
    matrix.setBrightness(currentDisplayBrightness);
  }
  if(--xp < -maxTextDisplacement) {
    xp = matrix.width();
  }
  matrix.show();
  delay(50);
  
}

void staticText(String text) {
  matrix.clear();
  matrix.setCursor(0,0);
  matrix.print(text.c_str());
  matrix.show();
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.persistent(false);
  WiFi.forceSleepWake();
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  if(!MDNS.begin(mqtt_client_name, WiFi.localIP())) {
    Serial.println("Error setting up MDNS Responder");
  } else {
    Serial.print("MDNS Setup for mqtt_client_name: ");
    Serial.println(mqtt_client_name);
  }

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  delay(150);

}

void setDisplayBrightnessByLight(int aLight) {
  if(!displayBrightnessAuto) return;
  // lower values are brighter
  int b = 0;
  if(aLight < DLEVEL_HIGH) b = displayBrightnessMap[3];
  else if(aLight < DLEVEL_MID) b = displayBrightnessMap[2];
  else if(aLight < DLEVEL_LOW) b = displayBrightnessMap[1];
  else b = displayBrightnessMap[0];
  if(b != matrix.getBrightness()) {
    Serial.print("A: ");
    Serial.print(aLight);
    Serial.print(" > change brightness to: ");
    Serial.println(b);
    matrix.setBrightness(b);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char p[length + 1];
  memcpy(p, payload, length);
  p[length] = '\0';

  StaticJsonDocument<200> doc;
  doc["time"] = millis();

  Serial.print(millis());
  Serial.print(" Message arrived [");
  Serial.print(topic);
  Serial.print("]");

  String tp = String(topic);
  int idx = tp.lastIndexOf("/");
  if (idx > 0) {
    tp = tp.substring(idx + 1);
  }
  Serial.print(" : ");
  Serial.print(tp);
  Serial.println();

  if(tp.equals("reset")) {
    ESP.reset();
    delay(500);
  }
  if(tp.equals("light")) {
    int aLight = analogLight(10);
    // Serial.print("A: ");
    // Serial.println(aLight);
    client.publish(out_topic_light.c_str(), String(aLight).c_str());
  }
  if(tp.equals("execute")) {
    // doReadings(true);
    doc["result"] = F("execute");
  } else if(tp.equals("reset")) {
    ESP.reset();
  } else
  if(tp.equalsIgnoreCase("static_text")) {
    displayText = 2;
    staticText(String(p));
    displayTextTimerId = displayTextTimer.setTimeout(textDuration, displayTextRun);
    doc["result"] = F("OK");
  } else
  // this is displaytime
  // if(tp.equalsIgnoreCase("textDurration")) {
  //   long td = atol(p);
  //   if(td > 0) {
  //     textDuration = td;
  //     doc["result"] = F("OK");
  //   } else {
  //     doc["result"] = F("NUM_ERROR");
  //   }
  // } else
  if(tp.equals("brightness")) {
    if(strcmp(p, "auto") == 0) {
      displayBrightnessAuto = true;
      doc["brightness"] = matrix.getBrightness();
      doc["result"] = F("Auto Brightness");
    } else {
      int b = atoi(p);
      if(b > 0 && b <= 255) {
        matrix.setBrightness(b);
        matrix.show();
        doc["brightness"] = matrix.getBrightness();
        displayBrightnessAuto = false;
      } else {
        doc["result"] = F("Set Brightness Failed");
      }
    }
  }
  if(tp.equals("message")) {
    //run message accross
    displayText = 1;
    messageScroll = String(p);
    displayTextTimerId = displayTextTimer.setTimeout(textDuration, displayTextRun);
    maxTextDisplacement = length * textPixelSize;
    doc["result"] = F("ok");
  }
  if(tp.equals("color") || tp.equals("colour")) {
    int r = atoi(strtok(p, ","));
    int g = atoi(strtok(NULL, ","));
    int b = atoi(strtok(NULL,","));
    Serial.print("Colour Set ");
    Serial.print(r); Serial.print(",");
    Serial.print(g); Serial.print(",");
    Serial.print(b); Serial.println(",");
    textColour = matrix.Color(r,g,b);
    matrix.setTextColor(textColour);
    JsonArray jar = doc.createNestedArray("colour_set");
    jar.add(r);
    jar.add(g);
    jar.add(b);
    //doc["colour_set"] = 
  }
  if(tp.equalsIgnoreCase("displaytime") || tp.equalsIgnoreCase("textDurration")) {
    int s = atoi(p);
    if(s > 0 && s < 300) {
      textDuration = s * 1000;
      doc["displaytime"] = textDuration;
      doc["result"] = F("Display Time Set to ms");
    } else {
      doc["result"] = F("Failed to set Display Time");
    }
  }
  if(tp.equals("settime")) {
    time_t tset = atol(p);
    if(tset > 0) {
      Serial.print("Time Set");
      Serial.println(tset);
      syncTime(tset);
      last_update_time = millis();
      displayTime();
    } else {
      Serial.println("Error setting time");
    }
  }
  char buf[512];
  serializeJson(doc,buf);
  client.publish(out_topic.c_str(), buf);
}

void blink(int cnt, unsigned long blink_speed) {
  for(int i=0; i < cnt; i++) {
    digitalWrite(LED, LOW);
    delay(blink_speed);
    digitalWrite(LED, HIGH);
    delay(blink_speed);
  }
  return;
}

int countPos = 0;
void counterStrip() {
  countPos++;
  if(countPos > matrix.width() ) countPos = 1;
  matrix.drawFastHLine(0,7,matrix.width(), matrix.Color(0,0,0));
  matrix.drawFastHLine(0,7,countPos, hLineColour);
  matrix.show();
  // Serial.println(countPos);
}

void matrixSetup() {
  matrix.begin();
  // matrix.setFont(&FreeMonoBold9pt7b);
  matrix.setTextWrap(false);
  matrix.setBrightness(dispBrightness);
  matrix.setTextColor(textColour);
  delay(10);
  
  // matrix.print("10");
  // matrix.setCursor(10,0);
  // matrix.print(":");
  // matrix.setCursor(14,0);
  // matrix.print("30P");

  // matrix.drawFastHLine(0,7,32, matrix.Color(128,0,0));
  // matrix.show();
  displayTime();
  timer.setInterval(1850, counterStrip);
  displayTextTimerId = displayTextTimer.setTimeout(textDuration, displayTextRun);
  displayTextTimer.disable(displayTextTimerId);
}


void setup() {
  // put your setup code here, to run once:
  // put your setup code here, to run once:.
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  Serial.println("Start");

  time_t test = 1574521008;
  //time_t test = 1574518174951;
  syncTime(test);

  setup_wifi();
  client.setClient(espClient);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  matrixSetup();
 
  
}

void displayTime() {
  if(displayText != 0 || last_update_time == 0) return;
  date = "";
  t = "";

  //time_t local, utc;
  
  // if(!timeClient.update()) {
  //   unsigned long epochTime =  timeClient.getEpochTime();

  //   // convert received time stamp to time_t object
  //   utc = epochTime;

  //   // Then convert the UTC UNIX timestamp to local time
  //   TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -300};  //UTC - 5 hours - change this as needed
  //   TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -360};   //UTC - 6 hours - change this as needed
  //   Timezone usEastern(usEDT, usEST);
  //   local = usEastern.toLocal(utc);

  //   setTime(local);
  // } else {
  //   Serial.println("ERROR --- NTP Failed ----");
  // }
  // Serial.println(now());

  // now format the Time variables into strings with proper names for month, day etc
  date += days[weekday()-1];
  date += ", ";
  date += months[month()-1];
  date += " ";
  date += day();
  date += ", ";
  date += year();

  // format the time to 12-hour format with AM/PM and no seconds
  t += hourFormat12();
  t += ":";
  if(minute() < 10)  // add a zero if minute is under 10
    t += "0";
  t += minute();
  t += " ";
  t += ampm[isPM()];

  // Display the date and time
  // Serial.println("");
  // Serial.print("Local date: ");
  // Serial.print(date);
  // Serial.println("");
  // Serial.print("Local time: ");
  // Serial.println(t);
  // Serial.println(second());

  String h = "";
  if(hourFormat12() < 10)
    h += "0";
  h += hourFormat12();

  matrix.clear();
  matrix.setCursor(0,0);
  matrix.print(h.c_str());
  matrix.setCursor(10,0);
  matrix.print(":");
  matrix.setCursor(14,0);

  String m = "";
  if(minute() < 10)
    m += "0";
  m += minute();
  if(isPM()) {
    m += "P";
  } else {
    m += "A";
  }
  
  matrix.print(m.c_str());
  countPos = second() / 2;
  matrix.drawFastHLine(0,7,matrix.width(), matrix.Color(0,0,0));
  matrix.drawFastHLine(0,7,countPos, hLineColour);
  matrix.show();
  
}



void reconnect() {
  // Loop until we're reconnected
  int retry = 0;
  while (!client.connected()) {
    blink(2, 100);
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    if (client.connect(mqtt_client_name.c_str(), willTopic.c_str(), 0, true, "offline")) {
      //if (client.connect(mqtt_client_name, mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 1 seconds");
      // Wait 5 seconds before retrying
      delay(1000);
      retry++;
    }
    if (retry > 10) break;
  }
  if (client.connected()) {
    client.publish(willTopic.c_str(), "online", true);
    delay(25);
    Serial.println("MQTT Subscribing...");
    client.subscribe(in_topic.c_str());
    delay(20);
    client.publish(out_topic_status.c_str(), WiFi.localIP().toString().c_str());
    delay(20);
    client.publish(MQTT_SEND_TIME, mqtt_client_name.c_str());
  }
}

int lastAnalogLight = 0;
int analogLight(int count) {
  int r = 0;
  for(int i=0; i < count; i++) {
    r = r + analogRead(A0);
    delay(10);
  }
  return (int) r/count;
}

int wifiRetryCount = 0;
void loop() {

  ArduinoOTA.handle();
  MDNS.update();
    
  // put your main code here, to run repeatedly:
  unsigned long now = millis();
  if(WiFi.status() == WL_CONNECTED && client.connected()) {
    digitalWrite(LED, HIGH);
    wifiRetryCount = 0;
  } else {
    digitalWrite(LED, LOW);
    // wl_status_t wifiState = WiFi.status();
    // if(wifiRetryCount > 3 && (wifiState == WL_DISCONNECTED || wifiState == WL_DISCONNECTED || wifiState == WL_CONNECT_FAILED)) {
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi/MQTT Issue?");
      wifiRetryCount++;
      if(wifiRetryCount > 60) {
        ESP.reset();
      }
      delay(500);
      return;
    }
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  timer.run();
  
  if((now - readings_last_checked) > do_readings_time) {
    //doReadings(); // don't do on timer, messes with mqtt?
    readings_last_checked = millis();
    // int a = analogRead(A0);
    // Serial.print("Reading ");
    // Serial.println(a);
    if(displayText == 0) blink(1, 200);
    displayTime();

    int aLight = analogLight(10);
    // Serial.print("A: ");
    // Serial.println(aLight);
    if(abs(aLight - lastAnalogLight) > 25) {
      client.publish(out_topic_light.c_str(), String(aLight).c_str());
      lastAnalogLight = aLight;
    }
    setDisplayBrightnessByLight(aLight);

    // Serial.println(WiFi.RSSI());
  }
  now = millis();
  if((now - last_update_time) > update_time) {
    client.publish(MQTT_SEND_TIME, mqtt_client_name.c_str());
    last_update_time = now;
    delay(5);
  }
  if(displayText == 1) doDisplayTextMessage();
  
  client.loop();
  
  displayTextTimer.run();
  
}
