#include "DHTesp.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// === Wi-Fi ===
#define WIFI_SSID "Sporki" // name of your wifi
#define WIFI_PASSWORD "123456" //Add here your wifi password 

// === Firebase ===
#define API_KEY "AIzaSyAWfIMqQwu_ZW6v-ESNDmtxK5Z_gyoKiOc"
#define DATABASE_URL "https://smartoth-90094-default-rtdb.firebaseio.com/"

// === Capteurs et actionneurs ===
int pinDHT = 18;
const int gasSensorPin = 34;
const int gasThreshold = 2000;

int servoPinWindows = 26;
int buzzerPin = 15;

int irPin = 32;
int irLed1 = 16;
int irLed2 = 17;
int irLed3 = 5;

int ledLivingRoom1 = 4;
int ledLivingRoom2 = 12;
int ledBathRoom = 0;
int ledKitchen = 2;
int ledBoysRoom = 22;
int ledGirlsRoom = 23;
int ledGarage = 25;

int servoPinGarage = 27;

Servo servoWindow;
Servo servoGarage;
DHTesp dht;

int pos = 5;
bool windowOpen = false;
String lastCommand = "";
int lastGaragePos = -1;

// === Firebase ===
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
unsigned long lastSend = 0;

void updateLEDs();
void processGarageCommand(bool shouldOpen, String source);

void setup() {
  Serial.begin(115200);

  // === Setup Capteurs ===
  dht.setup(pinDHT, DHTesp::DHT11);
  pinMode(gasSensorPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(irPin, INPUT);
  pinMode(irLed1, OUTPUT);
  pinMode(irLed2, OUTPUT);
  pinMode(irLed3, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // === Setup Servos ===
  servoWindow.attach(servoPinWindows);
  servoGarage.attach(servoPinGarage);
  servoWindow.write(pos);
  servoGarage.write(0);

  // === Setup LEDs ===
  pinMode(ledLivingRoom1, OUTPUT);
  pinMode(ledLivingRoom2, OUTPUT);
  pinMode(ledBathRoom, OUTPUT);
  pinMode(ledKitchen, OUTPUT);
  pinMode(ledBoysRoom, OUTPUT);
  pinMode(ledGirlsRoom, OUTPUT);
  pinMode(ledGarage, OUTPUT);

  // === Connexion Wi-Fi ===
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnecté au Wi-Fi");

  // === Synchroniser l'heure pour SSL ===
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nHeure synchronisée.");

  // === Connexion Firebase ===
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup OK");
    signupOK = true;
  } else {
    Serial.printf("Erreur Firebase Signup : %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // Éteindre LEDs IR par défaut
  digitalWrite(irLed1, LOW);
  digitalWrite(irLed2, LOW);
  digitalWrite(irLed3, LOW);

  // Update Firebase every 5 seconds
  if (Firebase.ready() && (millis() - lastSend > 5000)) {
    lastSend = millis();
    
    TempAndHumidity data = dht.getTempAndHumidity();
    float temp = data.temperature;
    float hum = data.humidity;
    int gasValue = analogRead(gasSensorPin);
    int detection = digitalRead(irPin);

    Serial.println("Température: " + String(temp, 2) + "°C");
    Serial.println("Humidité: " + String(hum, 1) + "%");
    Serial.println("Valeur gaz : " + String(gasValue));

    // Envoi à Firebase avec vérification
    if (!Firebase.RTDB.setFloat(&fbdo, "/globalData/temp", temp))
      Serial.println("Erreur envoi température : " + fbdo.errorReason());
      
    if (!Firebase.RTDB.setFloat(&fbdo, "/globalData/hum", hum))
      Serial.println("Erreur envoi humidité : " + fbdo.errorReason());

    if (!Firebase.RTDB.setInt(&fbdo, "/globalData/gaz", gasValue > gasThreshold ? 1 : 0))
      Serial.println("Erreur envoi gaz : " + fbdo.errorReason());

    // Gaz détecté : ouverture fenêtre
    if (gasValue > gasThreshold && !windowOpen) {
      Serial.println("⚠ Gaz détecté ! Ouverture de la fenêtre...");
      digitalWrite(buzzerPin, HIGH);
      for (pos = 5; pos <= 170; pos++) {
        servoWindow.write(pos);
        delay(10);
      }
      windowOpen = true;
      delay(3000);
    } else if (gasValue <= gasThreshold && windowOpen) {
      digitalWrite(buzzerPin, LOW);
      for (pos = 170; pos >= 5; pos--) {
        servoWindow.write(pos);
        delay(10);
      }
      windowOpen = false;
    }

    // IR : obstacle détecté
    if (detection == LOW) {
      digitalWrite(irLed1, HIGH);
      digitalWrite(irLed2, HIGH);
      digitalWrite(irLed3, HIGH);
      Serial.println("✅ Obstacle détecté !");
      Firebase.RTDB.setInt(&fbdo, "/globalData/motion", 1);
    } else {
      Firebase.RTDB.setInt(&fbdo, "/globalData/motion", 0);
    }

    // Commande garage via Firebase
    if (Firebase.RTDB.getBool(&fbdo, "/garage/garageDoor")) {
      bool firebaseCmd = fbdo.boolData();
      processGarageCommand(firebaseCmd, "Firebase");
    }
  }

  // Commande série pour ouvrir/fermer garage
  if (Serial.available()) {
    String cmd = Serial.readString();
    cmd.trim();
    if (cmd == "open" || cmd == "1") {
      processGarageCommand(true, "Série");
    } else if (cmd == "close" || cmd == "0") {
      processGarageCommand(false, "Série");
    }
  }

  updateLEDs();

  Serial.println("---");
  delay(500);
}

void updateLEDs() {
  if (Firebase.RTDB.getInt(&fbdo, "/livingRoom/led"))
    analogWrite(ledLivingRoom1, map(fbdo.intData(), 0, 100, 0, 255));
  
  if (Firebase.RTDB.getInt(&fbdo, "/livingRoom/led"))
    analogWrite(ledLivingRoom2, map(fbdo.intData(), 0, 100, 0, 255));

  if (Firebase.RTDB.getInt(&fbdo, "/bathroom/led"))
    analogWrite(ledBathRoom, map(fbdo.intData(), 0, 100, 0, 255));

  if (Firebase.RTDB.getInt(&fbdo, "/kitchen/led"))
    analogWrite(ledKitchen, map(fbdo.intData(), 0, 100, 0, 255));

  if (Firebase.RTDB.getInt(&fbdo, "/boysRoom/led"))
    analogWrite(ledBoysRoom, map(fbdo.intData(), 0, 100, 0, 255));

  if (Firebase.RTDB.getInt(&fbdo, "/girlsRoom/led"))
    analogWrite(ledGirlsRoom, map(fbdo.intData(), 0, 100, 0, 255));

  if (Firebase.RTDB.getInt(&fbdo, "/garage/led"))
    analogWrite(ledGarage, map(fbdo.intData(), 0, 100, 0, 255));
}

void processGarageCommand(bool shouldOpen, String source) {
  String cmd = shouldOpen ? "open" : "close";
  
  if (cmd != lastCommand) {
    lastCommand = cmd;

    if (shouldOpen && lastGaragePos != 90) {
      digitalWrite(buzzerPin, HIGH);
      Serial.println("[" + source + "] Ouverture du garage...");
      for (int p = 0; p <= 90; p++) {
        servoGarage.write(p);
        delay(15);
      }
      lastGaragePos = 90;
      digitalWrite(buzzerPin, LOW);
    }
    else if (!shouldOpen && lastGaragePos != 0) {
      digitalWrite(buzzerPin, HIGH);
      Serial.println("[" + source + "] Fermeture du garage...");
      for (int p = 90; p >= 0; p--) {
        servoGarage.write(p);
        delay(15);
      }
      lastGaragePos = 0;
      digitalWrite(buzzerPin, LOW);
    }
    else {
      Serial.println("[" + source + "] Commande déjà appliquée.");
    }
  }
}
