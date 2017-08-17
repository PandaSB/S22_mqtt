
/* #define MQTT_MAX_PACKET_SIZE 1024 must ne update in PubSubClient.h */
#define PIN_LED   13
#define PIN_RELAI 12
#define PIN_BTN   00

#define BUTTON_CHECK_PERIODE 0.1
#define BUTTON_CHECK_LOOP     10   /*1 / BUTTON_CHECK_PERIODE*/

#define DEFAULT_MQTT_SERVER "192.168.1.201"
#define DEFAULT_MQTT_USER ""  //s'il a été configuré sur Mosquitto
#define DEFAULT_MQTT_PASSWD "" //
#define DEFAULT_MQTT_OUTTOPIC "domoticz/out"
#define DEFAULT_MQTT_INTOPIC  "domoticz/in"
#define DEFAULT_MQTT_DOMOTICZ_ID "95"
#define DEFAULT_MQTT_MSG    "{\"command\": \"switchlight\", \"idx\": %s, \"switchcmd\": \"%s\", \"level\": 100}"

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>          //https://bblanchon.github.io/ArduinoJson/
#include <ArduinoOTA.h>


Ticker ticker;
Ticker ticker_btn; 
uint16_t counter_tick = 0;
WiFiManager wifiManager;



WiFiClient espClient;
PubSubClient client(espClient);
char mqtt_server[40];
char mqtt_user[40];
char mqtt_passwd[40];
char mqtt_inTopic[40];
char mqtt_outTopic[40];
char mqtt_domoticz_id[10];
char mqtt_msg[80];
char msg[80] ;




void reset_board (void)
{
    ticker.attach(0.2, tick);
   ticker_btn.detach();
   while (digitalRead(PIN_BTN) == 0 ) {}
   Serial.println("Reset board");
   delay (1000);
   ESP.reset();

}


void tick()
{
  //toggle state
  int state = digitalRead(PIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(PIN_LED, !state);     // set pin to the opposite state
}

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


void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  DynamicJsonBuffer jsonBuffer;
/*
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  */
  JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) {
    Serial.println("parseObject() failed");
    return;
  }
  uint16_t idx = root[String("idx")];
  uint16_t nvalue = root[String("nvalue")];
/*  
  Serial.print("idx : ");  Serial.println(idx);
  Serial.print("nvalue : ");  Serial.println(nvalue);
*/
  if ( idx == atoi (mqtt_domoticz_id) )
  {
    digitalWrite (PIN_RELAI , (nvalue == 0) ? LOW : HIGH);
  }
}

//Reconnexion
void reconnect() {
  //Boucle de reconnexion
  while (!client.connected()) {
    Serial.print("Connexion au serveur MQTT...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_passwd)) {
      Serial.println("OK");
      client.subscribe(mqtt_outTopic); 
      sprintf (msg, mqtt_msg,mqtt_domoticz_id,"Off");
      client.publish(mqtt_inTopic,msg);
    } else {
      Serial.print("KO, erreur : ");
      Serial.println(client.state());
      Serial.print("Server  ");Serial.print(mqtt_server) ;
      Serial.print("User  ");Serial.print(mqtt_user) ;Serial.print(" password   ");Serial.println(mqtt_passwd) ; 
      Serial.println(" On attend 5 secondes avant de recommencer");
      delay(5000);
    }
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(PIN_LED,   OUTPUT);
  pinMode(PIN_RELAI, OUTPUT);
  pinMode(PIN_BTN,   INPUT);
  digitalWrite(PIN_RELAI, LOW);

  ticker.attach(0.6, tick);
  ticker_btn.attach (BUTTON_CHECK_PERIODE,tick_btn);
  
  wifiManager.setAPCallback(configModeCallback);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", DEFAULT_MQTT_SERVER, 40);
  wifiManager.addParameter(&custom_mqtt_server);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", DEFAULT_MQTT_USER, 40);
  wifiManager.addParameter(&custom_mqtt_user);
  WiFiManagerParameter custom_mqtt_passwd("passwd", "mqtt password", DEFAULT_MQTT_PASSWD, 40);
  wifiManager.addParameter(&custom_mqtt_passwd);
  WiFiManagerParameter custom_mqtt_inTopic("intopic", "mqtt in topic", DEFAULT_MQTT_INTOPIC, 40);
  wifiManager.addParameter(&custom_mqtt_inTopic);
  WiFiManagerParameter custom_mqtt_outTopic("outtopic", "mqtt out topic", DEFAULT_MQTT_OUTTOPIC, 40);
  wifiManager.addParameter(&custom_mqtt_outTopic);    
  WiFiManagerParameter custom_mqtt_domoticz_id("domoticz_id", "domoticz Id", DEFAULT_MQTT_DOMOTICZ_ID, 10);
  wifiManager.addParameter(&custom_mqtt_domoticz_id);  
  WiFiManagerParameter custom_mqtt_msg("msg", "domoticz msg", DEFAULT_MQTT_MSG, 80);
  wifiManager.addParameter(&custom_mqtt_msg);  
  
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

  client.setServer(mqtt_server, 1883);    //Configuration de la connexion au serveur MQTT
  client.setCallback(mqtt_callback);
  
  ArduinoOTA.setHostname("Relai_S22"); // on donne une petit nom a notre module
  ArduinoOTA.begin(); // initialisation de l'OTA

}


// the loop function runs over and over again forever
void loop() {
    if (!client.connected()) {
    reconnect();
  }
  client.loop();
  ArduinoOTA.handle(); 

}
