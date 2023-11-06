#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <base64.h>

#define ledPin 21     
#define SS_PIN 5     
#define RST_PIN 22    
#define H 17
#define BUZZER_PIN 16
#define DAT 35
#define RST 34
#define CLK 6

WebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN); // SS_PIN, RST_PIN

String registeredCards[] = {"F3F1A603", "F79A9B60"};

unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000;
bool isLedOn = false;
bool buzzerEnabled = false;
bool cardDetected = false;
bool pirState = LOW;          // PIR 센서의 현재 상태
bool pirStatePrev = LOW;      // PIR 센서의 이전 상태

const char *ssid = "U+NetC230";
const char *password = "DDAE022471";
String wifi_ssid = "";
String wifi_password = "";

const char *host = "https://kapi.kakao.com/v2/api/talk/memo/default/send";
String access_token;

#define APP_KEY "7635611997061935e79d9162d7edefe6"
#define REFRESH_TOKEN "wuKqM1ddXUXwxv5qxc4GAOrSIHoCONRpIgUKKiVPAAABi0iuIwgh5oEAb4_jFQ"

// int pir = LOW;
// int val = 0;

// String ledOn = "감지모드";
// String ledOff = "초기모드";
// String rstMsg = "Reset Complete";
// String bon = "경고음 동작 시작";
// String bff = "경고음 동작 중지";

void setup() {
  Serial.begin(115200);
  
  pinMode(ledPin, OUTPUT); // LED Pin
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer Pin
  pinMode(H, INPUT); // PIR Sensor Pin

  //정적 IP주소를 할당함
  IPAddress ip(192, 168, 123, 108); 
  IPAddress gateway(192, 168, 123, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(8, 8, 8, 8);  // Google DNS
  WiFi.config(ip, gateway, subnet, dns); // DNS 서버 정보 추가

  ledcSetup(0, 5000, 8); // Initialize channel 0 with a frequency of 5000 Hz and 8-bit resolution
  ledcAttachPin(BUZZER_PIN, 0); // Attach channel 0 to the buzzer pin (GPIO 16)
  
  
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID-NFC 태그를 RFID 리더기에 탭.");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
  
  server.on("/ledOn", ledOnMode);
  server.on("/ledOff", ledOffMode);
  server.on("/Rst", rest);
  server.on("/buzOff", bf);
  server.on("/buzOn", bo);
  
  server.on("/setWifi", HTTP_POST, [](){
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    wifi_ssid = ssid;
    wifi_password = password;
    server.send(200, "text/html", "Wi-Fi 설정 업데이트");
  });
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long currentTime = millis();

  if (buzzerEnabled) {
    tone(BUZZER_PIN, 1000);
    delay(500);
    noTone(BUZZER_PIN);
    delay(500);
  }

  if (rfid.PICC_IsNewCardPresent()) {
    String cardUID = "";
    if (rfid.PICC_ReadCardSerial()) {
      for (byte i = 0; i < rfid.uid.size; i++) {
        cardUID += String(rfid.uid.uidByte[i], HEX);
      }
      cardUID.toUpperCase();

      bool cardMatched = false;
      for (int i = 0; i < sizeof(registeredCards) / sizeof(registeredCards[0]); i++) {
        if (cardUID.equals(registeredCards[i])) {
          cardMatched = true;
          break;
        }
      }

      if (cardMatched) {
        cardDetected = true;
        lastReadTime = currentTime;
      }

      if (currentTime - lastReadTime >= readInterval) {
        cardDetected = !cardDetected;
        MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
        Serial.print("RFID/NFC tag type: ");
        Serial.println(rfid.PICC_GetTypeName(piccType));
        lastReadTime = currentTime;
      }
    }
  }

   


  if (cardDetected) {
    isLedOn = true;
    int val = digitalRead(H);
    digitalWrite(ledPin, HIGH);
    
    if (val == HIGH && buzzerEnabled) {
      for (int i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 1000);
        delay(500);
        noTone(BUZZER_PIN);
        delay(500);
      }
    }
    if(val == HIGH){
      if (updateAccessToken()) {
      sendMessage();
    }
    }
    Serial.println(val);
    delay(100);
  } else {
    digitalWrite(ledPin, LOW);
    isLedOn = false;
    noTone(BUZZER_PIN);
  }

  
  
}

void sendMessage() {
  HTTPClient http;
  if (!http.begin(host)) {
    Serial.println("\nfailed to begin http\n");
  }
  http.addHeader("Authorization", "Bearer " + access_token);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int http_code;
  String data = "template_object={\"object_type\": \"text\",\"text\": \"감지! 움직임이 감지되었습니다! " + String(millis()) + "\",\"link\": {\"web_url\": \"https://www.naver.com\",\"mobile_web_url\": \"https://www.naver.com\"},\"button_title\": \"Check Now\"}";
  Serial.println(data);
  http_code = http.sendRequest("POST", data);
  Serial.print("HTTP Response code: ");
  Serial.println(http_code);
  String response;
  if (http_code > 0) {
    response = http.getString();
    Serial.println(response);
  }
  http.end();
}

String extractString(String str, String startString, String endString) {
  int index1 = str.indexOf(startString) + startString.length();
  int index2 = str.indexOf(endString, index1);
  String value = str.substring(index1, index2);
  return value;
}

bool updateAccessToken() {
  HTTPClient http;
  String url = "https://kauth.kakao.com/oauth/token";
  if (!http.begin(url)) {
    Serial.println("\nfailed to begin http\n");
  }
  int http_code;
  String client_id = String(APP_KEY);
  String refresh_token = String(REFRESH_TOKEN);
  String data = "grant_type=refresh_token&client_id=" + client_id + "&refresh_token=" + refresh_token;
  Serial.println(data);
  http_code = http.POST(data);
  Serial.print("HTTP Response code: ");
  Serial.println(http_code);
  String response;
  if (http_code > 0) {
    response = http.getString();
    Serial.println(response);
    access_token = extractString(response, "{\"access_token\":\"", "\"");
    http.end();
    return true;
  }
  http.end();
  return false;
}

void ledOnMode() {
  isLedOn = true;
  server.send(200, "text/html", "ledOn");
  digitalWrite(ledPin, HIGH);
  while (isLedOn) {
    server.handleClient();
    int val = digitalRead(H);
    if (val == HIGH){
    if (val == HIGH && buzzerEnabled) {
      for (int i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 1000);
        delay(500);
        noTone(BUZZER_PIN);
        delay(500);
      }
    }
    if(val == HIGH){
      if (updateAccessToken()) {
      sendMessage();
    }
    }
     }else {
      noTone(BUZZER_PIN);
    }
    Serial.println(val);
    delay(100);
  }
}

void ledOffMode() {
  server.send(200, "text/html", "정상 작동중");
  digitalWrite(ledPin, LOW);
}

void rest() {
  server.send(200, "text/html", "초기모드");
  isLedOn = false;
  digitalWrite(ledPin, LOW);
  buzzerEnabled = false;
}

void bo() {
  buzzerEnabled = true;
  noTone(BUZZER_PIN);
  server.send(200, "text/html", "경고음 작동");
  delay(100);
}

void bf() {
  buzzerEnabled = false;
  server.send(200, "text/html", "경고음 중지");
}
