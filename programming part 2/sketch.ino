//Include libraries
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

//pins declaration
#define BUZZER 18
#define LED 25
#define PB_CANCEL 33
#define PB_OK 32
#define PB_UP 34
#define PB_DOWN 35
#define DHTpin 15
const int LDRpin = 36;
const int servoPin = 12;

//Wifi 
#define NTP_SERVER "pool.ntp.org"
//int UTC_OFFSET =  0;
const int UTC_OFFSET_DST =  0;


//OLED 
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0X3c 

//declare DHT, OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  //OLED display
DHTesp DHT;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Servo servo;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Preferences preferences;

float temperature;
float humidity;

//time
int day;
int hr;
int mint;
int sec;

//alarms
const int n_alarm = 3;
int alarms[n_alarm][2];             // Alarms will be saved in 2D array in form of (hr,mint)
bool alarm_triggered[n_alarm] ;
bool alarm_enable = true;
bool DHT_alert_enable = true;

//Menu
int current_mode = 0;
int max_modes = 4;
String main_menu[] = {"1-Set Time", "2-Set Alarms", "3-Disable all Alarms", "4-Disable T&H Alert"};

int current_zone = 35;
String time_zones[] = {"-12:00", "-11:30", "-11:00", "-10:30", "-10:00", "-9:30", "-9:00", "-8:30", "-8:00", "-7:30", "-7:00", "-6:30", "-6:00", "-5:30", "-5:00","-4:30", "-4:00","-3:30", "-3:00","-2:30", "-2:00","-1:30", "-1:00","-0:30",
                        "+0:00", "+0:30", "+1:00", "+1:30", "+2:00", "+2:30", "+3:00", "+3:30", "+4:00", "+4:30", "+5:00", "+5:30", "+6:00", "+6:30", "+7:00", "+7:30","+8:00","+8:30","+9:00","+9:30","+10:00","+10:30","+11:00","+11:30","+12:00"};

char tempAr[6]="30";
char LDR_chr[6];
float LDR_val=0.0;
int min_Angle;
int crlFact;
int angle=0;
bool isSchedule=false;
unsigned long scheduleOnTime;


//tones
int notes_count = 8;
int notes[] = {262, 294, 330, 349, 292, 440, 494, 523};

//functions
void read_DHT();
void check_DHT();
void update_time();
void check_alarm();
void ring_alarm();
void print_line(String txt, int col, int row, int txt_size);
void print_time();
int wait_for_button_press();
void goto_menu();
void set_alarm(int h, int m);

void setup() {
  preferences.begin("Time Zone", false);
  unsigned int tz = preferences.getUInt("tz", 35);

  const int UTC_OFFSET  =   (tz-24)*1800;

  //declare pinmodes
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_DOWN, INPUT);

  // DHT setup
  DHT.setup(DHTpin, DHTesp::DHT22);

  // display settup
  Serial.begin(115200);
  // initialize WiFi
  WiFi.begin("Wokwi-GUEST", "", 6);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        display.clearDisplay();
        print_line("Connecting to WIFI", 0, 0, 2);
    }

    display.clearDisplay();
    print_line("Connected to WIFI", 0, 0, 2);


  // initiating OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  delay(500);

  display.clearDisplay();
  print_line("smart Medibox...", 0, 10, 2);
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  display.clearDisplay();

  servo.attach(servoPin, 500, 2400);
  setupWifi();
  setupMqtt();
  timeClient.begin();
  timeClient.setTimeOffset(5.5*3600);

}

void loop() {

  check_alarm();
  check_DHT();
  print_time();
  
  if (digitalRead(PB_OK) == LOW) {
    delay(200);
    go_to_main_menu();
  }

  if (!mqttClient.connected()){
        connectToBroker();
  }

  updateTemp();
  LDR_val = 1-analogRead(LDRpin)/4063.0;
  String(LDR_val, 2).toCharArray(LDR_chr, 6);
  mqttClient.loop();
  mqttClient.publish("200003P_Temp", tempAr);
  mqttClient.publish("200003P_Inten", LDR_chr);
  checkSchedule();
  delay(1000);
  angle=min_Angle + (180-min_Angle)*crlFact*LDR_val;
  servo.write(angle);
  delay(50);

  Serial.println(angle);
  

}

void updateTemp(){
    TempAndHumidity data = DHT.getTempAndHumidity();
    String(data.temperature, 2).toCharArray(tempAr, 6);
}

void checkSchedule(){
    if(isSchedule){
        unsigned long currentTime=getTime();
        if(currentTime>scheduleOnTime){
            buzzer_on(true);
            isSchedule=false;
            Serial.println("Scheduled ON");
            mqttClient.publish("200003PONOFF", "1");
            mqttClient.publish("schedule_ON", "0");
        }
    }
}

unsigned long getTime(){
    timeClient.update();
    return timeClient.getEpochTime();
}

void buzzer_on(bool on){
    if(on){
        tone(BUZZER, 255);

            
    }else{
        noTone(BUZZER);
    }
}


void connectToBroker()
{
    while (!mqttClient.connected())
    {
        Serial.println("Attempting MQTT connect");
        if (mqttClient.connect("ESP32-4546"))
        {
            Serial.println("connected");
            mqttClient.subscribe("min_angle");
            mqttClient.subscribe("CF");
            mqttClient.subscribe("200003P_Buzzer");
            mqttClient.subscribe("Switch_On");
            
        }
        else
        {
            Serial.print("failed");
            Serial.print(mqttClient.state());
            delay(5000);
        }
    }
}

void setupWifi()
{
    WiFi.begin("Wokwi-GUEST", "");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("Wifi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void setupMqtt()
{
    mqttClient.setServer("test.mosquitto.org", 1883);
    mqttClient.setCallback(receiveCallback);
}

void receiveCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arriced [");
    Serial.print(topic);
    Serial.print("] ");

    char payloadCharAr[length];
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
        payloadCharAr[i] = (char)payload[i];
    }

    Serial.println();

    if (strcmp(topic, "MA") == 0)
    {

        min_Angle = atoi(payloadCharAr);
    }
    else if (strcmp(topic, "CF") == 0)
    {

        crlFact = atoi(payloadCharAr);
    }else if(strcmp(topic,"Buzzer")==0){
          buzzer_on(payloadCharAr[0]=='1'  );
        
    }else if(strcmp(topic,"SwitchedOn")==0){
          if(payloadCharAr[0]=='N'){
            isSchedule=false;
          }else{
            isSchedule=true;
  scheduleOnTime=atol(payloadCharAr);
          }
        
    }

}

void read_DHT() {
  TempAndHumidity DHTdata = DHT.getTempAndHumidity();
  temperature = DHTdata.temperature;
  humidity = DHTdata.humidity;
  return;
}

void check_DHT() {
  read_DHT();
  if (DHT_alert_enable) {
    bool set_alert = false;
    if (temperature > 32) {
      display.clearDisplay();
      print_line("TEMP HIGH", 0, 0, 2);
      set_alert = true;
    }
    else if (temperature< 26) {
      display.clearDisplay();
      print_line("TEMP LOW", 0, 0, 2);
      set_alert = true;
    }

    if (humidity > 80) {
      print_line("HUMIDITY  HIGH", 0, 30, 2);
      set_alert = true;
    }
    else if (humidity < 60) {
      print_line("HUMIDITY  LOW", 0, 30, 2);
      set_alert = true;
    }

    while (set_alert && DHT_alert_enable) {
      if (digitalRead(PB_CANCEL) == LOW) {
        DHT_alert_enable = false;
        main_menu[3] = "6-Enable T&H Alert";
        delay(200);
        break;
      }
      tone(BUZZER, notes[5]);
      digitalWrite(LED, HIGH);
      delay(500);
      noTone(BUZZER);
      digitalWrite(LED, LOW);
      if (digitalRead(PB_CANCEL) == LOW) {
        DHT_alert_enable = false;
        main_menu[3] = "6-Enable T&H Alert";
        delay(200);
        break;
      }
      delay(300);
    }
  }
}



void update_time() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  hr = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  mint = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond, 3, "%S", &timeinfo);
  sec = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay, 3, "%d", &timeinfo);
  day = atoi(timeDay);

}


void check_alarm() {
  update_time();
  if (alarm_enable) {
    for (int i = 0; i < n_alarm; i++) {
      if (alarm_triggered[i] == false && alarms[i][0] == hr && alarms[i][1] == mint) {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}


void ring_alarm() {
  display.clearDisplay();
  print_line("Take", 0, 10, 2);
  print_line("Medicines", 0, 32, 2);
  digitalWrite(LED, HIGH);

  bool break_happened = false;

  //Ring the buzzer to alert
  while (break_happened == false  && digitalRead(PB_CANCEL) == HIGH) {
    for (int i = 0; i < notes_count; i++) {
      if (digitalRead(PB_CANCEL) == LOW) {
        delay(200);
        break_happened = true;
        break;
      }
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }

    for (int j = notes_count; j > 0; j--) {
      if (digitalRead(PB_CANCEL) == LOW) {
        delay(200);
        break_happened = true;
        break;
      }
      tone(BUZZER, notes[j]);
      delay(500);
      noTone(BUZZER);
      delay(200);
    }
  }
  digitalWrite(LED, LOW);
  display.clearDisplay();
}


//print functions
void print_line(String txt, int col, int row, int txt_size) {

  display.setTextSize(txt_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(col, row);
  display.println(txt);
  display.display();
}

void print_time() {
  display.clearDisplay();
  // display time
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 10);
  display.print(String(hr));
  display.setCursor(30, 10);
  display.print(":");
  display.setCursor(40, 10);
  display.print(String(mint));
  display.setCursor(60, 10);
  display.print(":");
  display.setCursor(70, 10);
  display.print(String(sec));

  //display temperature and humidity
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("T:" + String(temperature, 2) + "C");
  display.setCursor(72, 35);
  display.print("H:" + String(humidity, 1) + "%");
  display.setCursor(0, 52);
  display.print("Main Menu : Press OK");
  display.display();
}


int wait_for_button_press() {
  while (true) {
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }

    else if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }

    else if (digitalRead(PB_OK) == LOW) {
      delay(200);
      return PB_OK;
    }

    else if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      return PB_CANCEL;
    }

    update_time();
  }
}


//Menu
void go_to_main_menu() {

  while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    print_line(main_menu[current_mode], 0, 0, 2);

    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      current_mode += 1;
      current_mode = current_mode % max_modes;
    }

    else if (pressed == PB_DOWN) {
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
    }

    else if (pressed == PB_OK) {
      run_mode(current_mode);
    }

    else if (pressed == PB_CANCEL) {
      break;
    }
  }
}


void set_time() {

while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    print_line(time_zones[current_zone], 0, 0, 2);

    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      current_zone += 1;
      current_zone = current_zone % 49;
    }

    else if (pressed == PB_DOWN) {
      current_zone -= 1;
      if (current_zone < 0) {
        current_zone = 48;
      }
    }

    else if (pressed == PB_OK) {
      preferences.putUInt("tz", current_zone);
      preferences.end();
      display.clearDisplay();
      print_line("device will be restarted soon...",0,0,1);
      delay(3000);
      ESP.restart();
    }

    else if (pressed == PB_CANCEL) {
      break;
    }
  }
  display.clearDisplay();
  print_line("Time is set", 0, 0, 2);
  delay(1000);
}


void set_alarm_menu() {
int i =0;
while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    print_line("Alarm "+String(i+1), 0, 0, 2);

    int pressed = wait_for_button_press();

    if (pressed == PB_UP) {
      i += 1;
      i = i % n_alarm;
    }

    else if (pressed == PB_DOWN) {
      i -= 1;
      if (i < 0) {
        i = n_alarm - 1;
      }
    }

    else if (pressed == PB_OK) {
      set_alarm(i);
      break;
    }

    else if (pressed == PB_CANCEL) {
      break;
    }
  }

  display.clearDisplay();
  print_line("alarm is set", 0, 0, 2);
  delay(1000);
}


void set_alarm(int alarm) {

  int temp_hour = alarms[alarm][0];
  while (true) {
    display.clearDisplay();
    print_line("Enter hour: " + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_hour += 1;
      temp_hour = temp_hour % 24;
    }

    else if (pressed == PB_DOWN) {
      temp_hour -= 1;
      if (temp_hour < 0) {
        temp_hour = 23;
      }
    }

    else if (pressed == PB_OK) {
      alarms[alarm][0] = temp_hour;
      break;
    }

    else if (pressed == PB_CANCEL) {
      break;
    }
  }

  int temp_minute = alarms[alarm][1];
  while (true) {
    display.clearDisplay();
    print_line("Enter minute: " + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_minute += 1;
      temp_minute = temp_minute % 60;
    }

    else if (pressed == PB_DOWN) {
      temp_minute -= 1;
      if (temp_minute < 0) {
        temp_minute = 59;
      }
    }

    else if (pressed == PB_OK) {
      alarms[alarm][1] = temp_minute;
      alarm_triggered[alarm] = false;
      break;
    }

    else if (pressed == PB_CANCEL) {
      break;
    }
  }

  display.clearDisplay();
  print_line("Alarm is set", 0, 0, 2);
  delay(1000);
}


void run_mode(int mode) {
  if (mode == 0) {
    set_time();
  }

  else if (mode ==1) {
    set_alarm_menu();
  }

  else if (mode == 2) {
    alarm_enable = !alarm_enable;
    display.clearDisplay();
    if (alarm_enable) {
      main_menu[2] = "3-Disable Alarms";
      print_line("Alarms are enabled", 0, 0, 2);
    }
    else {
      main_menu[2] = "3-Enable Alarms";
      print_line("Alarms are disabled", 0, 0, 2);
    }
    delay(1000);
  }

  else if (mode == 3) {
    DHT_alert_enable = !DHT_alert_enable;
    display.clearDisplay();
    if (DHT_alert_enable) {
      print_line("H&T Alert is enabled", 0, 0, 2);
      main_menu[3] = "3-Disable T&H Alert";
    }
    else {
      print_line("H&T Alert is disabled", 0, 0, 2);
      main_menu[3] = "3-Enable T&H Alert";
    }
    delay(1000);
  }
}
