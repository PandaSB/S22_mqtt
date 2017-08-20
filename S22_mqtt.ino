/*
MIT License

Copyright (c) 2017 Stéphane BARTHELEMY

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file S22_mqtt.ino
 * @author BARTHELEMY STEPHANE 
 * @date August 19th 2017
 * @brief controle by mqtt a S22 power switch 
 */


#define S22_HARDWARE
//#define S20_HARDWARE
//#define ESP01_HARDWARE

#ifdef S20_HARDWARE
#define PIN_LED   13
#define PIN_RELAI 12
#define PIN_BTN   00
#endif

#ifdef S22_HARDWARE
#define PIN_LED       13
#define PIN_RELAI     12
#define PIN_BTN       00
#define PIN_JACK_PIN2 04
#define PIN_JACK_TIP  14

#define DHTPIN 14     // what digital pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#endif

#ifdef ESP01_HARDWARE
#define PIN_RELAI 02
#define PIN_BTN   00
#define PIN_LED   03
#endif

/*
INCLUDES 
*/
#include <FS.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://bblanchon.github.io/ArduinoJson/
#include <PubSubClient.h>
#include <Ticker.h>
#ifdef DHTPIN
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#endif

/*
GLOBALS DEFINES
*/
/* #define MQTT_MAX_PACKET_SIZE 1024 must ne update in PubSubClient.h */

#define BUTTON_CHECK_PERIODE 0.1
#define BUTTON_CHECK_LOOP     10   /*1 / BUTTON_CHECK_PERIODE*/
#ifdef DHTPIN
#define TEMP_CHECK_PERIODE    15
#endif


#define DEFAULT_MQTT_SERVER "192.168.1.201"
#define DEFAULT_MQTT_USER ""
#define DEFAULT_MQTT_PASSWD ""
#define DEFAULT_MQTT_OUTTOPIC "domoticz/out"
#define DEFAULT_MQTT_INTOPIC  "domoticz/in"
#define DEFAULT_MQTT_DOMOTICZ_ID "95"
#define DEFAULT_MQTT_MSG    "{\"command\": \"switchlight\", \"idx\": %s, \"switchcmd\": \"%s\", \"level\": 100}"
#define DEFAULT_MQTT_DOMOTICZ_TEMP_ID "97"
#define DEFAULT_MQTT_TEMP_MSG    "{\"idx\": %s, \"nvalue\": 0, \"svalue\": \"%s;%s\", \"level\": 100}"



#ifdef DHTPIN
DHT_Unified dht(DHTPIN, DHTTYPE);
#endif 

/*
GLOBAL VARIABLES
*/
Ticker ticker;
Ticker ticker_btn; 
#ifdef DHTPIN
Ticker ticker_temp; 
#endif

uint16_t counter_tick = 0;
WiFiManager wifiManager;
ESP8266WebServer server(80);
String webPage = "";
MDNSResponder mdns;
WiFiClient espClient;
PubSubClient client(espClient);
char mqtt_server[40]      = DEFAULT_MQTT_SERVER;
char mqtt_user[40]        = DEFAULT_MQTT_USER;
char mqtt_passwd[40]      = DEFAULT_MQTT_PASSWD;
char mqtt_inTopic[40]     = DEFAULT_MQTT_INTOPIC;
char mqtt_outTopic[40]    = DEFAULT_MQTT_OUTTOPIC;
char mqtt_domoticz_id[10] = DEFAULT_MQTT_DOMOTICZ_ID;
char mqtt_msg[80]         = DEFAULT_MQTT_MSG;
#ifdef DHTPIN
char mqtt_domoticz_temp_id[10] = DEFAULT_MQTT_DOMOTICZ_TEMP_ID;
char mqtt_temp_msg[80]         = DEFAULT_MQTT_TEMP_MSG;
#endif

char msg[80] ;

bool shouldSaveConfig = false;
bool Readtemp_flag = true ;

/**
 * @breif callback to save config 
 */
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/**
 * @brief List of command to restart the ESP 8266
 */
void reset_board (void)
{
    ticker.attach(0.2, tick);
   ticker_btn.detach();
   while (digitalRead(PIN_BTN) == 0 ) {}
   Serial.println("Reset board");
   delay (1000);
   ESP.reset();

}

/**
 * @brief control blink of led 
 */
void tick()
{
  //toggle state
  int state = digitalRead(PIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(PIN_LED, !state);     // set pin to the opposite state
}


/**
 * @brief control status of button , check long or short press 
 */
void tick_btn()
{
  int state = digitalRead(PIN_BTN);
  if (state == 0)
  {
     counter_tick ++ ; 
     if (counter_tick == ( BUTTON_CHECK_LOOP * 4 ))
     {
       Serial.println("Reset Wifi parameters");
       wifiManager.resetSettings();
       reset_board ();
     }
     
  }
  else 
  {
    if ( counter_tick > 0)
    {
        int state = digitalRead(PIN_RELAI);  // get the current state of GPIO1 pin
        digitalWrite(PIN_RELAI, !state);     // set pin to the opposite state
        sprintf (msg, mqtt_msg,mqtt_domoticz_id,(state == 0) ?  "On" : "Off");
        client.publish(mqtt_inTopic,msg);
    }
    counter_tick = 0 ; 
  }  
}

#ifdef DHTPIN
void tick_temp (void) {
  Readtemp_flag = true ; 
}
#endif

/**
 * @brief Confugure WIFI
 */
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


/**
 * @brief Parse mqtt message from domoticz and control relay 
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) {
    Serial.println("parseObject() failed");
    return;
  }
  uint16_t idx = root[String("idx")];
  uint16_t nvalue = root[String("nvalue")];
  if ( idx == atoi (mqtt_domoticz_id) )
  {
    digitalWrite (PIN_RELAI , (nvalue == 0) ? LOW : HIGH);
  }
}



/**
 * connect to mqtt broker 
 */
void reconnect() {
  //Boucle de reconnexion
  while (!client.connected()) {
    char szText[24];
    sprintf (szText,"ESP_%d_Client",ESP.getChipId());
    Serial.print("Connect to MQTT server ...");
    if (client.connect(szText, mqtt_user, mqtt_passwd)) {
      Serial.println("OK");
      client.subscribe(mqtt_outTopic); 
      sprintf (msg, mqtt_msg,mqtt_domoticz_id,"Off");
      client.publish(mqtt_inTopic,msg);
    } else {
      Serial.print("KO, error : ");
      Serial.println(client.state());
      Serial.print("Server  ");Serial.print(mqtt_server) ;
      Serial.print("User  ");Serial.print(mqtt_user) ;Serial.print(" password   ");Serial.println(mqtt_passwd) ; 
      Serial.println(" On attend 5 secondes avant de recommencer");
      delay(5000);
    }
  }
}

/**
 * @breif Initialisation 
 */
void setup() {

  Serial.begin(115200);

  pinMode(PIN_LED,   OUTPUT);
  pinMode(PIN_RELAI, OUTPUT);
  pinMode(PIN_BTN,   INPUT);
  digitalWrite(PIN_RELAI, LOW);

  ticker.attach(0.6, tick);
  ticker_btn.attach (BUTTON_CHECK_PERIODE,tick_btn);
#ifdef DHTPIN
  ticker_temp.attach (TEMP_CHECK_PERIODE,tick_temp);
#endif

  //SPIFFS.format();
  //wifiManager.resetSettings();

#ifdef DHTPIN
  dht.begin();
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  dht.humidity().getSensor(&sensor);
#endif

  //mount FS  ; 
  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          if (json["mqtt_server"]) strcpy(mqtt_server, json["mqtt_server"]);
          if (json["mqtt_user"])  strcpy(mqtt_user, json["mqtt_user"]);
          if (json["mqtt_passwd"])  strcpy(mqtt_passwd, json["mqtt_passwd"]);
          if (json["mqtt_inTopic"])  strcpy(mqtt_inTopic, json["mqtt_inTopic"]);
          if (json["mqtt_outTopic"])  strcpy(mqtt_outTopic, json["mqtt_outTopic"]);
          if (json["mqtt_domoticz_id"])  strcpy(mqtt_domoticz_id, json["mqtt_domoticz_id"]);
          if (json["mqtt_msg"])  strcpy(mqtt_msg, json["mqtt_msg"]); 
#ifdef DHTPIN
          if (json["mqtt_domoticz_temp_id"])  strcpy(mqtt_domoticz_id, json["mqtt_domoticz_temp_id"]);
          if (json["mqtt_temp_msg"])  strcpy(mqtt_msg, json["mqtt_temp_msg"]); 
#endif
        }
        configFile.close();

      }
     }
  } else {
    Serial.println("failed to mount FS");
  }

  
  wifiManager.setAPCallback(configModeCallback);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  wifiManager.addParameter(&custom_mqtt_server);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 40);
  wifiManager.addParameter(&custom_mqtt_user);
  WiFiManagerParameter custom_mqtt_passwd("passwd", "mqtt password", mqtt_passwd, 40);
  wifiManager.addParameter(&custom_mqtt_passwd);
  WiFiManagerParameter custom_mqtt_inTopic("intopic", "mqtt in topic", mqtt_inTopic, 40);
  wifiManager.addParameter(&custom_mqtt_inTopic);
  WiFiManagerParameter custom_mqtt_outTopic("outtopic", "mqtt out topic", mqtt_outTopic, 40);
  wifiManager.addParameter(&custom_mqtt_outTopic);    
  WiFiManagerParameter custom_mqtt_domoticz_id("domoticz_id", "domoticz Id", mqtt_domoticz_id, 10);
  wifiManager.addParameter(&custom_mqtt_domoticz_id);  
  WiFiManagerParameter custom_mqtt_msg("msg", "domoticz msg", mqtt_msg, 80);
  wifiManager.addParameter(&custom_mqtt_msg);  
#ifdef DHTPIN
  WiFiManagerParameter custom_mqtt_domoticz_temp_id("domoticz_temp_id", "domoticz temp Id", mqtt_domoticz_temp_id, 10);
  wifiManager.addParameter(&custom_mqtt_domoticz_temp_id);  
  WiFiManagerParameter custom_mqtt_temp_msg("temp msg", "domoticz temp msg", mqtt_temp_msg, 80);
  wifiManager.addParameter(&custom_mqtt_temp_msg);  
#endif
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    reset_board();
  }
  Serial.println("connected...:)");
  ticker.detach();
  digitalWrite(PIN_LED, LOW);

  strcpy(mqtt_server,custom_mqtt_server.getValue());
  strcpy(mqtt_user,custom_mqtt_user.getValue());
  strcpy(mqtt_passwd,custom_mqtt_passwd.getValue());
  strcpy(mqtt_inTopic,custom_mqtt_inTopic.getValue());
  strcpy(mqtt_outTopic,custom_mqtt_outTopic.getValue());
  strcpy(mqtt_domoticz_id,custom_mqtt_domoticz_id.getValue());
  strcpy(mqtt_msg,custom_mqtt_msg.getValue());
#ifdef DHTPIN
  strcpy(mqtt_domoticz_temp_id,custom_mqtt_domoticz_temp_id.getValue());
  strcpy(mqtt_temp_msg,custom_mqtt_temp_msg.getValue());
#endif 
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_passwd"] = mqtt_passwd;
    json["mqtt_inTopic"] = mqtt_inTopic;
    json["mqtt_outTopic"] = mqtt_outTopic;
    json["mqtt_domoticz_id"] = mqtt_domoticz_id;
    json["mqtt_msg"] = mqtt_msg ;
#ifdef DHTPIN
    json["mqtt_domoticz_temp_id"] = mqtt_domoticz_temp_id;
    json["mqtt_temp_msg"] = mqtt_temp_msg ;
#endif
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    
  }

  client.setServer(mqtt_server, 1883);    //set mqtt server. 
  client.setCallback(mqtt_callback);

  { 
    int state = digitalRead(PIN_RELAI);
    webPage += "<center><h1>ESP_";
    webPage += ESP.getChipId(); 
    webPage += "</h1>";
    webPage += "<p>";
    webPage += "<a href=\"socket1On\"><button>ON</button></a>";
    webPage += "<br>";
    webPage += "<a href=\"socket1Off\"><button>OFF</button></a>";
    webPage += "</p></center>";
  }
    
  char szText[16];
  sprintf (szText,"ESP_%d",ESP.getChipId());
  if (mdns.begin(szText, WiFi.localIP())) {
    Serial.println("MDNS responder started"); 
  }

 server.on("/", [](){
    server.send(200, "text/html", webPage);
  });
  server.on("/socket1On", [](){
    server.send(200, "text/html", webPage);
    int state = 0; 
    digitalWrite(PIN_RELAI, !state);     // set pin to the opposite state
    sprintf (msg, mqtt_msg,mqtt_domoticz_id,(state == 0) ?  "On" : "Off");
    client.publish(mqtt_inTopic,msg);
   });
  server.on("/socket1Off", [](){
    server.send(200, "text/html", webPage);
    int state = 1; 
    digitalWrite(PIN_RELAI, !state);     // set pin to the opposite state
    sprintf (msg, mqtt_msg,mqtt_domoticz_id,(state == 0) ?  "On" : "Off");
    client.publish(mqtt_inTopic,msg);  });
  server.begin();
  Serial.println("HTTP server started");
}


/**
 * @breif main loop 
 */
void loop() {
    char szTemp[10];
    char szHum[10];
          
    if (!client.connected()) {
    reconnect();
  }

#ifdef DHTPIN
  if (Readtemp_flag== true) {
    Readtemp_flag =  false ; 
    sensors_event_t event;  
    dht.temperature().getEvent(&event);
    Serial.print("DHT : ");
    if (!isnan(event.temperature)) { 
      dtostrf(event.temperature, 4, 2, szTemp);
    }
    dht.humidity().getEvent(&event);
    if (!isnan(event.relative_humidity)) {
      dtostrf(event.relative_humidity, 4, 2, szHum);   
    }
    sprintf (msg, mqtt_temp_msg,mqtt_domoticz_temp_id,szTemp, szHum);
    client.publish(mqtt_inTopic,msg);
  }
#endif 
  
  client.loop();
  server.handleClient();
  

}
