
// purpose: collect data and upload to gc2 server at 5mn intervals

#include "SparkFunMAX17043.h" // Include the SparkFun MAX17043 library, battery gauge
#include "SparkFunLSM9DS1.h" // include Sparkfun LSM9DS1 library, IMU
#include "math.h"
#include "gc_client.h"
#include "common.h"

LSM9DS1 imu;
#define LSM9DS1_M	0x1E
#define LSM9DS1_AG	0x6B

#define FIRMWARE_VERSION 1

GcClient gc_client;

int firmware_version = FIRMWARE_VERSION;

#define MANAGE_WIFI true // whether to switch off wifi in batch mode
#define DATA_TRANSFER_DELAY 500
#define COLLECT_DATA_FREQUENCY 250      // 250ms

bool serial_debug = true;

// declare functions
int set_device_id(String command);
int set_mode(String command);
int device_util(String command);

void serial_log(const char *func, int line, String message) {
  Serial.printlnf("%s %s:%d %s", Time.timeStr().c_str(), func, line, message.c_str());
}

int set_device_id(String command) {
  uint32_t device_id = command.toInt();
  gc_client.set_device_id(device_id);
  return 0;
}

void validation_tone() {
  tone(A4, 600, 150);
  delay(150);
  tone(A4, 900, 150);
}


int device_util(String command) {
  if(command=="test_serial") {
    DEBUG_LOG("serial test");
    return 0;
  } else if(command=="setup_serial") {
    Serial.begin(9600);
    return 0;
  } else if(command=="test_tone") {
    tone(A4, 600, 500);
  } else if(command.startsWith("cfg=")) {
    // format: cfg=dev2.photozzap.com,7001,2341234
    int i = command.indexOf("=");
    int j = command.indexOf(",");
    int k = command.indexOf(",", j+1);

    if(i == -1 || j == -1 || k == -1) {
      DEBUG_LOG("config parsing error: " +  command);
    }
    // DEBUG_LOG(String(i) + " " + String(j) + " " + String(k) + " " + String(l));

    // substring between i and j is hostname
    String hostname = command.substring(i+1,j);
    String port = command.substring(j+1,k);
    String deviceId = command.substring(k+1);

    DEBUG_LOG("config: hostname: " + hostname + " port: " + port + " deviceId: " + deviceId);
    validation_tone();

  }


  return 0;
}

void setup() {
  pinMode(A0, INPUT);
  pinMode(A4, OUTPUT);

  if (serial_debug) {
    Serial.begin(9600);
    // DEBUG_LOG("hit Enter to continue");
    // while(!Serial.available()) Particle.process();
    DEBUG_LOG("serial setup OK");
  }


  // IMU setup
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;

  if (!imu.begin())
  {
    DEBUG_LOG("Failed to communicate with LSM9DS1.");
  }

  Particle.function("device_id", set_device_id);
  Particle.function("device_util", device_util);
  Particle.function("set_mode", set_mode);

  Particle.variable("gc_version", firmware_version);

  gc_client.configure("dev2.photozzap.com", 7001, 42);

  // setup battery gauge
  lipo.begin();
  lipo.quickStart();

  DEBUG_LOG("started up");

  validation_tone();

}

int set_mode(String command) {
  if(command == "batch") {
    DEBUG_LOG("enable batch mode");
    if(MANAGE_WIFI) {
      DEBUG_LOG("turning off wifi");
      WiFi.off();
    }
    gc_client.set_mode(GC_MODE_BATCH);
  } else if (command == "realtime" ) {
    DEBUG_LOG("enable realtime mode");
    gc_client.set_mode(GC_MODE_REALTIME);
  } else if (command == "standby") {
    DEBUG_LOG("enable standby mode");
    gc_client.set_mode(GC_MODE_STANDBY);
  }

  return 0;
}

void report_battery_charge() {
  gc_client.battery_charge(lipo.getSOC());
}

void report_stats() {
  DEBUG_LOG("reporting stats");
  Particle.publish("upload_stats", gc_client.get_stats());
}


float get_gyro_max() {
  // retrieve the highest gyro rate on 3 axes
  imu.readGyro();
  float gyro_x = imu.calcGyro(imu.gx);
  float gyro_y = imu.calcGyro(imu.gy);
  float gyro_z = imu.calcGyro(imu.gz);

  return max(max(gyro_x, gyro_y), gyro_z);
}

void get_accel(float *accel_values) {
  imu.readAccel();
  accel_values[0] = imu.calcAccel(imu.ax);
  accel_values[1] = imu.calcAccel(imu.ay);
  accel_values[2] = imu.calcAccel(imu.az);
}

void collect_data() {
  uint16_t emg_value = analogRead(A0);
  float gyro_max = get_gyro_max();
  float accel_values[3];
  get_accel(accel_values);

  float accel_x = accel_values[0];
  float accel_y = accel_values[1];
  float accel_z = accel_values[2];

  gc_client.add_datapoint(emg_value, gyro_max, accel_x, accel_y, accel_z);

  if(gc_client.need_upload()){
    DEBUG_LOG("need to upload batch");
    // get battery data
    report_battery_charge();

    // turn on WiFi
    if(MANAGE_WIFI) {
      DEBUG_LOG("enabling wifi");
      WiFi.on();
    }
    DEBUG_LOG("wait for wifi to be available");
    // wait for wifi to be available
    waitFor(WiFi.ready, WIFI_MAX_WAIT);
    DEBUG_LOG("wifi available");

    gc_client.upload_batch();

    // turn off wifi
    if(MANAGE_WIFI) {
        DEBUG_LOG("disabling wifi");
        WiFi.off();
    }

  }
}

void loop() {
  collect_data();
  delay(COLLECT_DATA_FREQUENCY);
}
