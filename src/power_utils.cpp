 #include "configuration.h"
#include "power_utils.h"
#include "pins_config.h"
#include "logger.h"

#ifndef TTGO_T_Beam_S3_SUPREME_V3
#define I2C_SDA 21
#define I2C_SCL 22
#define IRQ_PIN 35
#endif

#ifdef TTGO_T_Beam_S3_SUPREME_V3
#define I2C0_SDA 17
#define I2C0_SCL 18
#define I2C1_SDA 42
#define I2C1_SCL 41
#define IRQ_PIN  40
#endif

#ifdef HAS_AXP192
XPowersAXP192 PMU;
#endif
#ifdef HAS_AXP2101
XPowersAXP2101 PMU;
#endif


extern Configuration    Config;
extern logging::Logger  logger;
extern bool             disableGPS;

bool    pmuInterrupt;
float   lora32BatReadingCorr = 6.5; // % of correction to higher value to reflect the real battery voltage (adjust this to your needs)

namespace POWER_Utils {

  bool   BatteryIsConnected = false;
  String batteryVoltage = "";
  String batteryChargeDischargeCurrent = "";

  double getBatteryVoltage() {
    #if defined(BATTERY_PIN) && !defned(HELTEC_V3_GPS) //It is likely that different boards will require different corrections.
    int adc_value = analogRead(BATTERY_PIN);
    double voltage = (adc_value * 3.3 ) / 4095.0;  // the battery voltage is divided by 2 with two 100kOhm resistors and connected to ADC1 Channel 7 -> pin 35
    return (2 * (voltage + 0.1)) * (1 + (lora32BatReadingCorr/100)); // 2 x voltage divider/+0.1 because ESP32 nonlinearity ~100mV ADC offset/extra correction
    #endif
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    return PMU.getBattVoltage() / 1000.0;
    #endif
    #if defined(HELTEC_V3_GPS)
    int adc_value = analogRead(BATTERY_PIN);
    double voltage = (adc_value * 3.3) / 4095.0;
    double inputDivider = (1.0 / (390.0 + 100.0)) * 100.0;  // The voltage divider is a 390k + 100k resistor in series, 100k on the low side. 
    return (voltage / inputDivider) + 0.3; // Yes, this offset is excessive, but the ADC on the ESP32s3 is quite inaccurate and noisy. Adjust to own measurements.
    #endif
    return 0;
  }

  String getBatteryInfoVoltage() {
    return batteryVoltage;
  }

  String getBatteryInfoCurrent() {
    return batteryChargeDischargeCurrent;
  }

  bool getBatteryInfoIsConnected() {
    return BatteryIsConnected;
  }

  void enableChgLed() {
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    PMU.setChargingLedMode(XPOWERS_CHG_LED_ON);
    #endif
  }

  void disableChgLed() {
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    PMU.setChargingLedMode(XPOWERS_CHG_LED_OFF);
    #endif
  }  

  bool isCharging() {
    #if defined(BATTERY_PIN)
    return 0;
    #endif
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    return PMU.isCharging();
    #endif
    return 0;
  }

  void handleChargingLed() {
    if (isCharging()) {
      enableChgLed();
    } else {
      disableChgLed();
    }
  }

  double getBatteryChargeDischargeCurrent() {
    #if defined(BATTERY_PIN)
    return 0;
    #endif
    #if defined(HAS_AXP192)
    if (PMU.isCharging()) {
      return PMU.getBatteryChargeCurrent();
    }
    return -1.0 * PMU.getBattDischargeCurrent();
    #endif
    #if defined(HAS_AXP2101)
    return PMU.getBatteryPercent();
    #endif
    return 0;
  }

  bool isBatteryConnected() {
    #if defined(BATTERY_PIN)
    if(getBatteryVoltage() > 1.0) {
      return true;
    } else {
      return false;
    }
    #endif
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    return PMU.isBatteryConnect();
    #endif
    return false;
  }

  void obtainBatteryInfo() {
    static unsigned int rate_limit_check_battery = 0;
    if (!(rate_limit_check_battery++ % 60))
      BatteryIsConnected = isBatteryConnected();
    if (BatteryIsConnected) {
      #if defined(BATTERY_PIN)
      batteryVoltage       = String(getBatteryVoltage(), 2);
      #endif
      #if defined(HAS_AXP2101)
      batteryVoltage       = String(PMU.getBattVoltage());
      #endif
      batteryChargeDischargeCurrent = String(getBatteryChargeDischargeCurrent(), 0);
    }
  }

  void batteryManager() {
    obtainBatteryInfo();
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    handleChargingLed();
    #endif
  }

  void activateMeasurement() {
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    PMU.disableTSPinMeasure();
    PMU.enableBattDetection();
    PMU.enableVbusVoltageMeasure();
    PMU.enableBattVoltageMeasure();
    PMU.enableSystemVoltageMeasure();
    #endif
  }

  void activateGPS() {
    #if defined(HAS_AXP192)
    PMU.setLDO3Voltage(3300);
    PMU.enableLDO3();
    #endif
    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.setALDO3Voltage(3300);
    PMU.enableALDO3(); 
    #endif
    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.setALDO4Voltage(3300);
    PMU.enableALDO4();
    #endif
  }

  void deactivateGPS() {
    #if defined(HAS_AXP192)
    PMU.disableLDO3();
    #endif
    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.disableALDO3();
    #endif
    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.disableALDO4();
    #endif
  }

  void activateLoRa() {
    #if defined(HAS_AXP192)
    PMU.setLDO2Voltage(3300);
    PMU.enableLDO2();
    #endif
    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.setALDO2Voltage(3300);
    PMU.enableALDO2();
    #endif
    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.setALDO3Voltage(3300);
    PMU.enableALDO3();
    #endif
  }

  void deactivateLoRa() {
    #if defined(HAS_AXP192)
    PMU.disableALDO2();
    #endif
    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.disableALDO2();
    #endif
    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    PMU.disableALDO3();
    #endif
  }

  bool begin(TwoWire &port) {
    #if defined(BATTERY_PIN)
    return true; // no powerManagment chip for this boards (only a few measure battery voltage).
    #endif
    #if defined(HAS_AXP192)
    bool result = PMU.begin(Wire, AXP192_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (result) {
      PMU.disableDC2();
      PMU.disableLDO2();
      PMU.disableLDO3();
      PMU.setDC1Voltage(3300);
      PMU.enableDC1();
      PMU.setProtectedChannel(XPOWERS_DCDC3);
      PMU.disableIRQ(XPOWERS_AXP192_ALL_IRQ);
    }
    return result;
    #endif

    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    bool result = PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (result) {
      PMU.disableDC2();
      PMU.disableDC3();
      PMU.disableDC4();
      PMU.disableDC5();
      PMU.disableALDO1();
      PMU.disableALDO4();
      PMU.disableBLDO1();
      PMU.disableBLDO2();
      PMU.disableDLDO1();
      PMU.disableDLDO2();
      PMU.setDC1Voltage(3300);
      PMU.enableDC1();
      PMU.setButtonBatteryChargeVoltage(3300);
      PMU.enableButtonBatteryCharge();
      PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    }
    return result;
    #endif

    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    bool result = PMU.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C1_SDA, I2C1_SCL);
    if (result) {
      PMU.disableDC2();
      PMU.disableDC3();
      PMU.disableDC4();
      PMU.disableDC5();
      PMU.disableBLDO1();
      PMU.disableBLDO2();
      PMU.disableDLDO1();
      PMU.disableDLDO2();
      PMU.setDC1Voltage(3300);
      PMU.enableDC1();
      PMU.setALDO1Voltage(3300);
      PMU.setButtonBatteryChargeVoltage(3300);
      PMU.enableButtonBatteryCharge();
      PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    }
    return result;
    #endif
    return false;
  }

  void setup() {
    Wire.end();
    #if defined(HAS_AXP192)
    Wire.begin(SDA, SCL);
    if (begin(Wire)) {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AXP192", "init done!");
    } else {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AXP192", "init failed!");
    }
    activateLoRa();
    if (disableGPS) {
      deactivateGPS();
    } else {
      activateGPS();
    }
    activateMeasurement();
    PMU.setChargerTerminationCurr(XPOWERS_AXP192_CHG_ITERM_LESS_10_PERCENT);
    PMU.setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
    PMU.setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_780MA);
    PMU.setSysPowerDownVoltage(2600);
    #endif

    #if defined(HAS_AXP2101) && !defined(TTGO_T_Beam_S3_SUPREME_V3)
    Wire.begin(SDA, SCL);
    if (begin(Wire)) {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AXP2101", "init done!");
    } else {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AXP2101", "init failed!");
    }
    #endif

    #if defined(TTGO_T_Beam_S3_SUPREME_V3)
    Wire1.begin(I2C1_SDA, I2C1_SCL);
    Wire.begin(I2C0_SDA, I2C0_SCL);
    if (begin(Wire1)) {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AXP2101", "init done!");
    } else {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AXP2101", "init failed!");
    }
    #endif
    
    #if defined(HAS_AXP2101)
    activateLoRa();
    if (disableGPS) {
      deactivateGPS();
    } else {
      activateGPS();
    }
    activateMeasurement();
    PMU.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);
    PMU.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_800MA);
    PMU.setSysPowerDownVoltage(2600);
    #endif
    #ifdef BATTERY_PIN
     pinMode(BATTERY_PIN, INPUT);    // This could or should be elsewhere, but this was my point of entry.  (Could be in main. HA5SZI)
  #endif
  }

  void lowerCpuFrequency() {
    #if defined(TTGO_T_Beam_V1_0) || defined(TTGO_T_Beam_V1_0_SX1268) || defined(TTGO_T_Beam_V1_2) || defined(ESP32_DIY_LoRa_GPS) || defined(TTGO_T_LORA32_V2_1_GPS) || defined(TTGO_T_LORA32_V2_1_TNC) || defined(ESP32_DIY_1W_LoRa_GPS) || defined(TTGO_T_Beam_V1_2_SX1262) || defined(TTGO_T_Beam_S3_SUPREME_V3) || defined(HELTEC_V3_GPS)
    if (setCpuFrequencyMhz(80)) {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "CPU frequency set to 80MHz");
    } else {
      logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "CPU frequency unchanged");
    }
    #endif
  }

  void shutdown() {
    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
    PMU.shutdown();
    #endif
  }

}