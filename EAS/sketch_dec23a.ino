#include <WiFi.h>
#include <PubSubClient.h>

// ==========================================
// 1. KONFIGURASI WIFI & MQTT
// ==========================================
const char* ssid = "Uuu"; 
const char* password = "ixhspwiw";

const char* mqtt_server = "broker.emqx.io";
const char* mqtt_topic = "upn/fauzan/motor/pwm";      // Topic Slider (HP -> ESP32) | Kirim 0-255
const char* mqtt_pub_topic = "upn/fauzan/motor/rpm_reading"; // Topic RPM (ESP32 -> HP)

WiFiClient espClient;
PubSubClient client(espClient);

// ==========================================
// 2. KONFIGURASI MOTOR & PIN
// ==========================================
int motor1Pin1 = 27;
int motor1Pin2 = 26;
int enable1Pin = 12;
const byte pin_rpm = 13;

// PWM Properties
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8; // 8-bit artinya nilai PWM 0 - 255

// ==========================================
// 3. VARIABEL RPM & PWM
// ==========================================
volatile unsigned long rev = 0;
unsigned long last_rev_count = 0;
unsigned long last_rpm_time = 0;

float rpm = 0;
float rpm_filtered = 0;

// Variabel Kontrol
int targetPWM = 0; // Nilai PWM langsung (0-255) yang diterima dari MQTT

unsigned long ts = 0, new_ts = 0;

// Interrupt Service Routine
void IRAM_ATTR isr() {
  rev++;
}

// ==========================================
// 4. FUNGSI WIFI & MQTT
// ==========================================
void setup_wifi() {
  delay(10);
  Serial.print("\nConnecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* message, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)message[i];
  }
  msg.trim();
  
  Serial.print("MQTT Received: ");
  Serial.println(msg);

  if (msg == "stop" || msg == "off") {
    targetPWM = 0; 
    Serial.println("Motor STOP (PWM 0)");
  } 
  else {
    float val = msg.toFloat();
    // Batas aman input PWM (harus 0 - 255 untuk resolusi 8-bit)
    if (val >= 0 && val <= 255) {
      targetPWM = (int)val;
      Serial.print("New PWM Set: ");
      Serial.println(targetPWM);
    }
    else if (val > 255) {
        targetPWM = 255; // Cap di maks jika slider berlebih
        Serial.println("Value too high, capped at 255");
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32_Caezar_" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_topic); 
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

// ==========================================
// 5. LOGIKA HITUNG RPM (Hanya Monitoring)
// ==========================================
void calculateRPM() {
  unsigned long current_time = millis();
  unsigned long time_elapsed = current_time - last_rpm_time;

  if (time_elapsed >= 1000) { 
    noInterrupts();
    unsigned long current_rev = rev;
    interrupts();
    
    int holes = 2; // Sesuaikan dengan jumlah lubang encoder disk
    float rotations = (float)(current_rev - last_rev_count) / holes;
    rpm = (rotations * 60000.0) / time_elapsed;
    
    rpm_filtered = 0.7 * rpm_filtered + 0.3 * rpm;
    
    last_rev_count = current_rev;
    last_rpm_time = current_time;
  }
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(enable1Pin, OUTPUT);
  pinMode(pin_rpm, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(pin_rpm), isr, RISING);

  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(enable1Pin, pwmChannel);
  
  // Set Arah Putaran Motor
  digitalWrite(motor1Pin1, HIGH);
  digitalWrite(motor1Pin2, LOW);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  ts = millis();
  last_rpm_time = millis();
}

void loop() {
  // 1. MQTT Handler
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 2. Terapkan PWM langsung ke Motor
  // Nilai ini diupdate langsung dari fungsi callback saat ada pesan masuk
  ledcWrite(pwmChannel, targetPWM);

  // 3. Baca RPM (Hanya untuk dikirim balik ke HP sebagai monitoring)
  calculateRPM();
  
  // 4. Kirim Data RPM ke HP (Setiap 1 detik)
  new_ts = millis();
  if (new_ts - ts >= 1000) { 
    ts = new_ts;
    
    // Debug ke Serial
    Serial.print("PWM Input: "); Serial.print(targetPWM);
    Serial.print(" | Actual RPM: "); Serial.println(rpm);

    // Kirim ke HP
    String dataRPM = String(rpm); 
    client.publish(mqtt_pub_topic, dataRPM.c_str()); 
  }
  
  delay(10); 
}