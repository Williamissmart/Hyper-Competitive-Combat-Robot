#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>


#define MPU9250_ADDR 0x68

// WiFi credentials for PC's hotspot
const char* ssid = "COMBATROBOT";      // Replace with your PC hotspot SSID
const char* password = "password123"; // Replace with your PC hotspot password

// PC server details
const char* serverIP = "192.168.137.1"; // Default gateway IP of PC hotspot (adjust if needed)
const uint16_t serverPort = 80;
const uint16_t localPort = 80;

WiFiUDP udp;

float gyroBiasZ = 0;
float heading = 0;
unsigned long lastUpdate = 0;
unsigned long lastCommand = 0;

Servo esc1, esc2, esc3;  // Create Servo object for ESC

const int escPin1 = 13;  // GPIO pin for ESC 1
const int escPin2 = 14;  // GPIO pin for ESC 2
const int escPin3 = 27;  // PWM-capable GPIO pin for ESC signal
const int minPulse = 1000;  // Full reverse (microseconds)
const int neutralPulseDrive = 1500;  // Neutral/stop (microseconds)
const int neutralPulseWeapon = 0; //stop for weapon
const int maxPulse = 2000;  // Full forward (microseconds)
const int armPulseDrive = 1500;  // Arming pulse (neutral for bidirectional)
const int armPulseWeapon = 1000;

uint8_t readByte(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.read();
}

void writeByte(uint8_t addr, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void readBytes(uint8_t addr, uint8_t reg, uint8_t count, uint8_t *dest) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, count);
  for (int i = 0; i < count; i++) {
    dest[i] = Wire.read();
  }
}

void setGyroRangeTo2000DPS() {
  // GYRO_CONFIG (0x1B): bits 4-3 = 11 for ±2000 dps
  uint8_t config = readByte(MPU9250_ADDR, 0x1B);
  config &= ~0x18;      // Clear bits 4 and 3
  config |= (0x03 << 3); // Set to 11 for ±2000 dps
  writeByte(MPU9250_ADDR, 0x1B, config);
}

void calibrateGyro(int samples = 500) {
  long sumZ = 0;
  Serial.println("Calibrating gyro (keep still)...");
  for (int i = 0; i < samples; i++) {
    uint8_t buffer[14];
    readBytes(MPU9250_ADDR, 0x3B, 14, buffer);
    int16_t gz = ((int16_t)buffer[12] << 8) | buffer[13];
    sumZ += gz;
    delay(5);
  }
  gyroBiasZ = sumZ / (float)samples;
  Serial.print("Gyro Z bias: ");
  Serial.println(gyroBiasZ);
}

void setup() {
  Serial.begin(115200);  // For debugging
  delay(1000);

    // Connect to PC's WiFi hotspot
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("Connected to PC hotspot.");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Start UDP
  udp.begin(localPort);


  // Allow allocation of all timers for Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
 
  esc1.setPeriodHertz(50);  // 50Hz for ESC 1
  esc2.setPeriodHertz(50);  // 50Hz for ESC 2
  esc3.setPeriodHertz(50);  // 50Hz for ESC 3
  esc1.attach(escPin1, minPulse, maxPulse);  // Attach ESC 1
  esc2.attach(escPin2, minPulse, maxPulse);  // Attach ESC 2
  esc3.attach(escPin3, minPulse, maxPulse);  // Attach ESC 3
 
  // Arming sequence for all ESCs
  esc1.writeMicroseconds(armPulseDrive);  // Neutral pulse
  esc2.writeMicroseconds(armPulseDrive);
  esc3.writeMicroseconds(armPulseWeapon);
  delay(10000);

  Serial.begin(115200);
  Wire.begin(18, 19); // ESP32 default: SDA 21, SCL 22

  writeByte(MPU9250_ADDR, 0x6B, 0x00); // Wake up
  setGyroRangeTo2000DPS();            // Set ±2000 dps

  calibrateGyro();
  lastUpdate = micros();
}

void loop() {
  static unsigned long lastLoop = 0;
  static int16_t lastEsc1Throttle = 0, lastEsc2Throttle = 0, lastEsc3Throttle = 0;
  //if (micros() - lastLoop < 25000) return; // 100ms loop
  lastLoop = micros();

  // Update heading
  static const float GYRO_SCALE = 16.4;      // LSB/°/s for ±2000 dps
  static const float MIN_CHANGE_DEG = 0.01;  // Ignore small changes
  uint8_t buffer[4];
  memcpy(&buffer, &heading, 4);
  readBytes(MPU9250_ADDR, 0x3B, 14, buffer);
  int16_t gz = ((int16_t)buffer[12] << 8) | buffer[13];
  float gz_dps = (gz - gyroBiasZ) / GYRO_SCALE;
  unsigned long now = micros();

  float dt = (now - lastUpdate) / 1000000.0;
  lastUpdate = now;
  float deltaHeading = gz_dps * dt;
  if (fabs(deltaHeading) >= MIN_CHANGE_DEG) {
    heading += deltaHeading;
    if (heading >= 360.0) heading -= 360.0;
    else if (heading < 0.0) heading += 360.0;
  }

  udp.beginPacket(serverIP, serverPort);
  udp.write((uint8_t*)&heading, sizeof(float));
  udp.endPacket();

      // Receive UDP packet: esc1Throttle, esc2Throttle, esc3Throttle (int16_t)
    int16_t esc1Throttle = lastEsc1Throttle;
    int16_t esc2Throttle = lastEsc2Throttle;
    int16_t esc3Throttle = lastEsc3Throttle;
    int packetSize = udp.parsePacket();
    Serial.println(packetSize);
    if (packetSize == 6) {  // 3 * int16_t = 6 bytes
      lastCommand = micros();
      char buffer[6];
      udp.read(buffer, 6);
      memcpy(&esc1Throttle, buffer, 2);
      memcpy(&esc2Throttle, buffer + 2, 2);
      memcpy(&esc3Throttle, buffer + 4, 2);
      Serial.printf("values are: ESC1=%d, ESC2=%d, ESC3=%d\n", esc1Throttle, esc2Throttle, esc3Throttle);
      if (esc1Throttle >= -100 && esc1Throttle <= 100 &&
          esc2Throttle >= -100 && esc2Throttle <= 100 &&
          esc3Throttle >= -100 && esc3Throttle <= 100) {
        lastEsc1Throttle = esc1Throttle;
        lastEsc2Throttle = esc2Throttle;
        lastEsc3Throttle = esc3Throttle;
        Serial.printf("Received: ESC1=%d, ESC2=%d, ESC3=%d\n", esc1Throttle, esc2Throttle, esc3Throttle);
      } else {
        Serial.println("ERR:Invalid throttle values: -100 to 100 expected");
        esc1Throttle, esc2Throttle, esc3Throttle = 0;
      }
    } else if (packetSize > 0) {
      Serial.printf("ERR:Invalid packet size: %d\n", packetSize);
    }

  // Set ESC PWM (1000–2000µs)
  int pulse1 = map(esc1Throttle, -100, 100, minPulse, maxPulse);
  int pulse2 = map(esc2Throttle, -100, 100, minPulse, maxPulse);
  int pulse3 = map(esc3Throttle, -100, 100, minPulse, maxPulse);
  esc1.writeMicroseconds(pulse1);
  esc2.writeMicroseconds(pulse2);
  esc3.writeMicroseconds(pulse3);

  // Timeout: Stop ESCs if no command received for 1 second
  if (micros() - lastCommand > 1000000) {
    esc1.writeMicroseconds(armPulseDrive);
    esc2.writeMicroseconds(armPulseDrive);
    esc3.writeMicroseconds(armPulseWeapon);
  }
}
