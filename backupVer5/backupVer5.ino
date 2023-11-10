/*
기능 :  안드로이드에서 HTTP 요청을 보내 센서 및 부저, led제어
        RFID를 이용한 센서 및 부저, led 제어
*/
//--------------------------------------------------------------------------------------
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <base64.h>
//--------------------------------------------------------------------------------------

#define ledPin 21     //RED LED
#define SS_PIN 5      //RFID
#define RST_PIN 22    //RFID
#define H 17          //pir 센서
#define BUZZER_PIN 16 //부저

//--------------------------------------------------------------------------------------

WebServer server(80);          //웹서버 포트 : 80
MFRC522 rfid(SS_PIN, RST_PIN); // SS_PIN, RST_PIN

//--------------------------------------------------------------------------------------

String registeredCards[] = {"F3F1A603", "F79A9B60"};  //RFID 지정한 UID 

//--------------------------------------------------------------------------------------

unsigned long lastReadTime = 0;           //RFID 읽는 딜레이 간격
const unsigned long readInterval = 1000;  //RFID 읽는 딜레이 간격 1초
bool isLedOn = false;                     //
bool buzzerEnabled = false;               //부저 상태 변경
bool cardDetected = false;                //RFID 상태 변경
bool pirState = LOW;                      // PIR 센서의 현재 상태
bool pirStatePrev = LOW;                  // PIR 센서의 이전 상태

//--------------------------------------------------------------------------------------

const char *ssid = "U+NetC230";           //esp32 접속할 와이파이 이름
const char *password = "DDAE022471";      //esp32 접속할 와이파이 비밀번호
String wifi_ssid = "";                    //안드로이드에서 보낸 문자열을 받아 와이파이 이름 업데이트
String wifi_password = "";                //안드로이드에서 보낸 문자열을 받아 와이파이 비밀번호 업데이트
const char *host = "https://kapi.kakao.com/v2/api/talk/memo/default/send";    //카톡 메시지를 보낼 카톡 API
String access_token;    //카톡으로 메시지를 보내기 access토큰 업데이트

//--------------------------------------------------------------------------------------

#define APP_KEY "7635611997061935e79d9162d7edefe6" //카톡 개발자 연결
#define REFRESH_TOKEN "wuKqM1ddXUXwxv5qxc4GAOrSIHoCONRpIgUKKiVPAAABi0iuIwgh5oEAb4_jFQ" //엑세스토큰을 업데이트

//-----------------------------------------------------------------------------------------------------------------↑설정
//-----------------------------------------------------------------------------------------------------------------↓setup
void setup() {
  Serial.begin(115200);    //Serial 통신
  SPI.begin();             //SPI 초기화 : 장치간의 데이터를 교환  
  rfid.PCD_Init();         //RFID 초기화

//--------------------------------------------------------------------------------------↓LED, BUZ, PIR

  pinMode(ledPin, OUTPUT);      // LED
  pinMode(BUZZER_PIN, OUTPUT);  // 부저
  pinMode(H, INPUT);            // PIR

//--------------------------------------------------------------------------------------↓ WIFI status

  //정적 IP주소를 할당함
  IPAddress ip(192, 168, 123, 108);       //연결할 와이파이 주소 할당 ip 192, 168, 123, 108
  IPAddress gateway(192, 168, 123, 1);    //공유기에 접속하기 위한 게이트 설정
  IPAddress subnet(255, 255, 255, 0);     // '''''' 서브넷 설정
  IPAddress dns(8, 8, 8, 8);              //카톡 메시지를 보내기 위한 도메인 설정
  WiFi.config(ip, gateway, subnet, dns);  // 위의 설정들을 저장

//--------------------------------------------------------------------------------------↓led, buz 컨트롤 함수

  ledcSetup(0, 5000, 8);                  //led 컨트롤러 함수 사용하지 않으면 에러남
  ledcAttachPin(BUZZER_PIN, 0);           //부저 컨트롤
  
//--------------------------------------------------------------------------------------↓시작 문구

  Serial.println("RFID-NFC 태그를 RFID 리더기에 탭.");

//--------------------------------------------------------------------------------------↓WIFI connecting
  
  WiFi.mode(WIFI_STA);         // 와이파이 모드 설정 클라이언트 모드로 설정
  /*WIFI_OFF: Wi-Fi를 비활성화
    WIFI_STA: Station (클라이언트) 모드로 설정
    WIFI_AP: 액세스 포인트 (AP) 모드로 설정
    WIFI_AP_STA: 동시에 AP 및 Station 모드로 설정.*/
  WiFi.begin(ssid, password);  //Wifi 연결
  
  while (WiFi.status() != WL_CONNECTED) {   //와이파이 상태가 연결이 될 때 까지 시도
    delay(500);
    Serial.print(".");
  }
 
  Serial.println("WiFi connected");         
  Serial.println(WiFi.localIP());           //연결된 와이파이 이름 프린트

 //--------------------------------------------------------------------------------------↓안드로이드로부터 요청받은 명령 이름 지정

  server.on("/operation", operation);     //주요기능 동작(센서, LED, 등)
  server.on("/ledOff", ledOffMode);   //LED Only OFF
  server.on("/Rst", rest);            //초기상태 모든 것들 OFF
  server.on("/buzOff", bf);           //부저 True상태 변경 val(pir) 값이 1이면 부저 울림
  server.on("/buzOn", bo);            //부저 false상태 변경 val(pir) 값이 1이어도 부저 울리지 않음
  server.on("/setWifi", HTTP_POST, [](){      //WIFI 이름, 비밀번호 상태 변경
    String ssid = server.arg("ssid");         //안드로이드와의 서버 통신으로 와이파이 이름 받아옴
    String password = server.arg("password"); //안드로이드와의 서버 통신으로 와이파이 비밀번호 받아옴
    wifi_ssid = ssid;                         //와이파이 이름 변경
    wifi_password = password;                 //비밀번호 변경
    server.send(200, "text/html", "Wi-Fi 설정 업데이트"); //서버로 텍스트 전송 안드로이드에서 텍스트를 받아 명령 실행
  });

//--------------------------------------------------------------------------------------

  server.begin();   //서버 시작
}
//-----------------------------------------------------------------------------------------------------------------↑setup
//-----------------------------------------------------------------------------------------------------------------↓loop
void loop() {
  server.handleClient();                //서버 요청
  unsigned long currentTime = millis(); //현재 시간을 가져옴 RFID 연속 인식 방지 딜레이

//--------------------------------------------------------------------------------------↓RFID UID 읽고 저장

  //카드가 감지 되면 //127번 괄호 시작
  if (rfid.PICC_IsNewCardPresent()) {   
    String cardUID = ""; //감지된 카드 UID를 변수에 저장

    //카드의 UID를 읽어와서
    if (rfid.PICC_ReadCardSerial()) {
      //카드의 UID를 16진수 문자열로 변환하여 저장
      for (byte i = 0; i < rfid.uid.size; i++) {
        cardUID += String(rfid.uid.uidByte[i], HEX);
      }
      cardUID.toUpperCase();  //문자열을 대문자로 저장
      //*Comment : 위에 지정한(25줄) UID가 16진수이면서 대문자임

//--------------------------------------------------------------------------------------↓카드 일치 확인

      bool cardMatched = false;   //등록된 카드와 일치하는지 저장하는 변수

      //인식된 카드가 등록된 카드와 UID가 일치하는지 여부 확인
      for (int i = 0; i < sizeof(registeredCards) / sizeof(registeredCards[0]); i++) {
        if (cardUID.equals(registeredCards[i])) {
          cardMatched = true;   //카드가 일치하면 true로 변경
          break;
        }
      }

//--------------------------------------------------------------------------------------↓ read 상태 변경 

      if (cardMatched) {
        cardDetected = true;        //카드 감지
        lastReadTime = currentTime; //마지막으로 카드 읽은 시간을 현재 시간으로 변경
      }

      //일정 시간 경과
      if (currentTime - lastReadTime >= readInterval) {
        cardDetected = !cardDetected;   //정한 시간(1초) 경과 후 카드상태 반전 가능 *연속 태그 방지를 위한 딜레이
        MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);    //태그 타입 가져옴
        Serial.print("RFID/NFC tag type: ");
        Serial.println(rfid.PICC_GetTypeName(piccType));    //태그 타입 표시
        lastReadTime = currentTime; //시간을 현재 시간으로 업데이트
      }
    }

  } //127번 괄호 끝
//--------------------------------------------------------------------------------------↓카드 감지 동작

  if (cardDetected) {                     //RFID 카드 인식 상태 True
    int val = digitalRead(H);             //pir값 read
    digitalWrite(ledPin, HIGH);
    cardDetected = true;                  //처음이랑 같은 부분이지만 넣어본 결과 좀 더 정밀하게 작동하는걸 확인함
    if (val == HIGH && buzzerEnabled) {   //val = 1 and buz = true -> 부저음 발생 띠 - 띠
        buz1();
    }
    if(val == HIGH){                          //val = 1 -> access 토큰 업데이트 및 카톡으로 메시지 전송  
      if (updateAccessToken()) {              //만약 access토큰이 업데이트 됐다면 메시지 전송
      sendMessage();                          //위 메시지 추가 comment : sendMessage만 사용 가능 단, 6시간마다 토큰을 업데이트 해야하기에
    }                                         //매번 업데이트 할 수 있게 수정                                      
    }
    Serial.println(val);
    delay(100);
  } else {
    
    digitalWrite(ledPin, LOW);
    isLedOn = false;
    cardDetected = false;
    noTone(BUZZER_PIN);
  }
  

}
//-----------------------------------------------------------------------------------------------------------------↑loop
//-----------------------------------------------------------------------------------------------------------------↓buz 소리 지정
void buz1() {
  tone(BUZZER_PIN, 500); // 주파수 500Hz
  delay(500);
  noTone(BUZZER_PIN);
}
//-----------------------------------------------------------------------------------------------------------------↑buz 소리 지정
//-----------------------------------------------------------------------------------------------------------------↓senMessage
void sendMessage() {
  HTTPClient http;
  if (!http.begin(host)) {
    Serial.println("\nfailed to begin http\n");
  }
  http.addHeader("Authorization", "Bearer " + access_token);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

//--------------------------------------------------------------------------------------↓카톡 메시지 전송
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

//--------------------------------------------------------------------------------------↓엑세스 토큰 업데이트
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
//-----------------------------------------------------------------------------------------------------------------↑senMessage
//-----------------------------------------------------------------------------------------------------------------↓안드로이드 요청 respond(ON)
void operation() {
  server.send(200, "text/html", "정상 작동"); //서버 통신 text를 보내고 결과를 안드로이드에서 받아 원하는 기능 구현
  cardDetected = true; //카드상태를 true로 변경 별도의 코드 없이 카드 enable 상태로 원하는 동작 모두 작동
}
//-----------------------------------------------------------------------------------------------------------------↑안드로이드 요청 respond(ON)
//-----------------------------------------------------------------------------------------------------------------↓안드로이드 요청 respond(OFF)
void ledOffMode() {
  server.send(200, "text/html", "정상 작동(ledOff)");   //263줄 설명
  digitalWrite(ledPin, LOW);                           //led만 끄는게 목적
  

}
//-----------------------------------------------------------------------------------------------------------------↑안드로이드 요청 respond(OFF)
//-----------------------------------------------------------------------------------------------------------------↓안드로이드 요청 respond(all false)
void rest() {
  server.send(200, "text/html", "초기모드(동작중지)");
  isLedOn = false;
  buzzerEnabled = false;
  cardDetected = false;
}
//-----------------------------------------------------------------------------------------------------------------↑안드로이드 요청 respond(all false)
//-----------------------------------------------------------------------------------------------------------------↓안드로이드 요청 respond(buz On)
void bo() {
  buzzerEnabled = true;
  noTone(BUZZER_PIN);
  server.send(200, "text/html", "경고음 작동");
  delay(100);
}
//-----------------------------------------------------------------------------------------------------------------↑안드로이드 요청 respond(buz On)
//-----------------------------------------------------------------------------------------------------------------↓안드로이드 요청 respond(buz Off)
void bf() {
  buzzerEnabled = false;
  server.send(200, "text/html", "경고음 중지");
}
