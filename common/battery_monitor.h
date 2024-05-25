#ifndef COMMON_BATTERY_MONITOR_H
#define COMMON_BATTERY_MONITOR_H


#include "analog_read.h"

 // Points of linear interpolation for battery percentage - Gizfan IMR18650, 2600mAh
#define BATMIN_LOADED     3.13   // Minimum battery voltage (0%), when loaded
#define BATMAX_LOADED     3.98   // Maximum battery voltage (100%), when loaded
#define BATMIN_UNLOADED   3.21   // Minimum battery voltage (0%), when unloaded  
#define BATMAX_UNLOADED   4.11   // Maximum battery voltage (100%), when unloaded


// RESINDEX determines the voltage resolution. This is relevant for Analog LEDs, which regulate output based on current voltage. To increase processing speed we'll keep voltage in integer until a float
// Lower resolution will increase measurement accuracy AND processing time
/*  RESINDEX    ProffieBoard LSB    Saberprop and UltraProffies LSB
      0             1.6 mV                1.1 mV
      1             3.2 mV                2.2 mV
      2             6.4 mV                4.4 mV
      3            12.9 mV                8.8 mV
      4            25.8 mV               17.6 mV
      5            51.6 mV               35.2 mV
*/


#ifndef NO_BATTERY_MONITOR
  #ifdef ARDUINO_ARCH_ESP32
    #include "esp32-hal-adc.h"
    #include "driver/adc.h"
    #include "esp_adc_cal.h"
    //#define CALIBRATED_VOLTAGE_LSB 0.001231671 // Calibrated voltageLSB - if defined it is used instead of .voltageLSB()
    //#define CALIBRATED_VOLTAGE_LSB 0.001142893  // by calc
    #define CALIBRATED_VOLTAGE_LSB   0.00120637

    template <uint8_t LSBINDEX=0>
  #else // STM
    // #define CALIBRATED_VOLTAGE_LSB 0.00875912 // Calibrated voltageLSB - if defined it is used instead of .voltageLSB()
    
    template <uint8_t LSBINDEX=0>
  #endif

    class BatteryMonitor : Looper, CommandParser, StateMachine {
    private:
      bool loaded_ = false;
      uint16_t low_count_ = 0;
      AnalogReader readerVbat_;
      float calibratedLSB = 0;
      #if defined(SABERPROP) && defined(ARDUINO_ARCH_STM32L4) // STM Saberprop and UltraProffies
        AnalogReader readerVRef_;
      #elif defined(ARDUINO_ARCH_ESP32)
        uint8_t unit = 1;
        int8_t channel = 0;
        esp_adc_cal_characteristics_t chars = {};
      #endif

    public:
      RangeStats<int32_t,7> iVoltage; // integer voltage -> gets converted to volts (float) only when requested - via battery(). EMA filter runs on this integer

      #if defined(SABERPROP) && defined(ARDUINO_ARCH_STM32L4) // STM Saberprop and UltraProffies read Vbat and Vref
        BatteryMonitor() : Looper(1000), readerVbat_(batteryLevelPin, INPUT , 10e-6),  readerVRef_(batteryRefPin, INPUT , 10e-6) {}  
      #else // Proffie Board and ESP SaberProp read VBat only
        BatteryMonitor() : Looper(1000) , readerVbat_(batteryLevelPin, INPUT , 10e-6) {}
      #endif
        

      const char* name() override { return "BatteryMonitor"; }

      // Voltage LSB - defined below in specializations, only for the allowed values of LSBINDEX
      // This is also the voltage change threshold for LED refresh, so lower LSB means more calculations
      float voltageLSB() { return 0; }

      // Get battery voltage as integer ( = multiple of voltageLSB )
      int32_t iBattery() {
        return iVoltage.avg >> LSBINDEX + 7;
      }

        // Get battery voltage as float ( = volts)
        float battery() { // __attribute__((optimize("Og"))) {
          #ifdef CALIBRATED_VOLTAGE_LSB
            return CALIBRATED_VOLTAGE_LSB * (float)iBattery();  // 1. Use FLASH calibrated LSB, if available
          #else
            if (calibratedLSB) return calibratedLSB * (float)iBattery();  // 2. Use RAM calibrated LSB, if available
            return voltageLSB() * (float)iBattery();    // 3. Use calculated (un-calibrated) LSB
          #endif
          // return (float)0.0012 * (float)iBattery() + (float)0.1125;
        }


        void SetLoad(bool on) {
          loaded_ = on;
          // STDOUT.print("Battery load = "); STDOUT.println(on);
        }

        bool low() const { return low_count_ > 1000; }

        float battery_percent() {
          // Energy is roughly proportional to voltage squared.
          float v = battery();  // battery voltage
          float p; // battery percentage  
          if (loaded_)  p = 100.0 * clamp((v * v - BATMIN_LOADED*BATMIN_LOADED) / (BATMAX_LOADED*BATMAX_LOADED - BATMIN_LOADED*BATMIN_LOADED), 0.01, 1);    
          else p = 100.0 * clamp((v * v - BATMIN_UNLOADED*BATMIN_UNLOADED) / (BATMAX_UNLOADED*BATMAX_UNLOADED - BATMIN_UNLOADED*BATMIN_UNLOADED), 0.01, 1);  
          return p;  
        }

        void Print() {
          if (battery() < 0.5) STDOUT.print("not connected");
          else { 
              float percent = battery_percent();
              if (percent<5) STDOUT.print("empty");
              else { 
                STDOUT.print((uint8_t)battery_percent()); 
                STDOUT.print("%"); 
              }
          }
        }

        void SetPinHigh(bool go_high) {
          #ifdef PROFFIEBOARD 
            if (go_high) {
              pinMode(batteryLevelPin, OUTPUT);
              digitalWrite(batteryLevelPin, HIGH);
            } 
            else
              pinMode(batteryLevelPin, INPUT);     
          #endif
      }

      // protected:
      public:

        void Setup() override {         
        }

        void Loop() override  {

          #ifdef ARDUINO_ARCH_STM32L4
            #ifdef PROFFIEBOARD // ProffieBoard
              STATE_MACHINE_BEGIN();
              while (true) {
                while (!readerVbat_.Start()) YIELD();
                while (!readerVbat_.Done()) YIELD(); 
                int32_t intVolt = readerVbat_.Value();   // integer voltage 
                intVolt <<= 7;   // scale with 2^7 before filtering, to minimize rounding errors
                iVoltage.Add (intVolt);    
                if (IsLow()) {
                  if (low_count_ < 10000) // don't overrun!
                    low_count_++;
                }
                else low_count_ = 0;    
              }
              STATE_MACHINE_END();                                    
            #else // STM Saberprop and UltraProffies
              STATE_MACHINE_BEGIN();
              while (true) {
                while (!readerVRef_.Start()) YIELD();
                while (!readerVRef_.Done()) YIELD();
                while (!readerVbat_.Start()) YIELD();
                while (!readerVbat_.Done()) YIELD();

                int32_t intVolt;   // integer voltage 
                intVolt = *((uint16_t*)0x1fff75aa);  // calibration value , Datasheet 3.15.1
                intVolt *= readerVbat_.Value();  
                // intVolt <<= 4;       
                intVolt <<= 8;    // scale with 2^8  before filtering, to minimize rounding errors      
                intVolt /= readerVRef_.Value();  // Voltage [V] = ADC_reading/4095 * 3 {volts} * 3 {resistors_ratio}
                // intVolt >>= LSBINDEX+1;             
                iVoltage.Add (intVolt);     
                if (IsLow()) {
                  if (low_count_ < 10000) // don't overrun!
                    low_count_++;
                }
                else low_count_ = 0;    
              }
              STATE_MACHINE_END();                        
            #endif
          #else // ESP SaberProp
            int32_t intVolt = readerVbat_.Value();
            intVolt <<= 7;   // scale with 2^7 before filtering, to minimize rounding errors
            iVoltage.Add (intVolt);
            if (IsLow()) {
              if (low_count_ < 10000) // don't overrun!
                low_count_++;
            }
            else low_count_ = 0;            
          #endif        
      }

      bool IsLow() {
        #ifdef PROFFIEBOARD
          if (USBD_Connected()) return false;
        #endif
        if (battery() < 0.5) return false;        // Battery isn't low if it's not connected at all.
        return battery() < (loaded_ ? BATMIN_LOADED : BATMIN_UNLOADED);
      }

      bool Parse(const char* cmd, const char* arg) override {
        // if (!strcmp(cmd, "battery_voltage")) {
        //   STDOUT.println(battery());
        //   return true;
        // }
      
        if (!strcmp(cmd, "battery")) {
          STDOUT.print(battery()); STDOUT.print("V = ");
          Print(); STDOUT.println("");
          // STDOUT.print("avgRaw = "); STDOUT.println(iBattery());
          return true;
        }
        
        if (!strcmp(cmd, "battery_calibration")) {          
          uint16_t milliVolts = atoi(arg);
          if (milliVolts < 3500 || milliVolts > 4000) {
            STDOUT.println ("FAILED - Calibrate between 3500 [mV] and 4000 [mV]. Reverted to factory calibration");
            calibratedLSB = 0;
            return true;
          }
          float vLSB = 1000 * milliVolts;    // known voltage / ADC reading = LSB
          vLSB /= iBattery();
          calibratedLSB = vLSB/1000000;
          STDOUT.print("Set CALIBRATED_VOLTAGE_LSB to "); STDOUT.print(vLSB); STDOUT.println(" E-6 today.");
          return true;
        }

        return false;
      }

      void Help() override {
        #if defined(COMMANDS_HELP) 
        STDOUT.println(" battery - show battery percent");
        STDOUT.println(" battery_voltage - show battery voltage");
        STDOUT.println(" battery_calibration <milliVolts> - calibrate battery monitor");
        #endif
      }

    
    };


    #ifdef ARDUINO_ARCH_STM32L4
      #ifdef PROFFIEBOARD // ProffieBoard
        template<>  // voltage LSB for LSBINDEX = 0
        float BatteryMonitor<0>::voltageLSB() { return 0.001611722; } // 1.6 mV

        template<> // voltage LSB for LSBINDEX = 1
        float BatteryMonitor<1>::voltageLSB() { return 0.003223443; } // 3.2 mV

        template<> // voltage LSB for LSBINDEX = 2
        float BatteryMonitor<2>::voltageLSB() { return 0.006446886; } // 6.4 mV

        template<> // voltage LSB for LSBINDEX = 3
        float BatteryMonitor<3>::voltageLSB() { return 0.012893773; } // 12.9 mV

        template<> // voltage LSB for LSBINDEX = 4
        float BatteryMonitor<4>::voltageLSB() { return 0.025787546; } // 25.8 mV

        template<> // voltage LSB for LSBINDEX = 5
        float BatteryMonitor<5>::voltageLSB() { return 0.051575092; } // 51.6 mV

        BatteryMonitor<2> battery_monitor;  // iVoltage LSB = 6.4mV 
      #else // STM Saberprop and UltraProffies
        template<>  // voltage LSB for LSBINDEX = 0
        float BatteryMonitor<0>::voltageLSB() { return 0.001098901; } // 1.1 mV

        template<> // voltage LSB for LSBINDEX = 1
        float BatteryMonitor<1>::voltageLSB() { return 0.002197802; } // 2.2 mV

        template<> // voltage LSB for LSBINDEX = 2
        float BatteryMonitor<2>::voltageLSB() { return 0.004395604; } // 4.4 mV

        template<> // voltage LSB for LSBINDEX = 3
        float BatteryMonitor<3>::voltageLSB() { return 0.008791209; } // 8.8 mV

        template<> // voltage LSB for LSBINDEX = 4
        float BatteryMonitor<4>::voltageLSB() { return 0.017582418; } // 17.6 mV

        template<> // voltage LSB for LSBINDEX = 5
        float BatteryMonitor<5>::voltageLSB() { return 0.035164835; } // 35.2 mV

        // BatteryMonitor<3> battery_monitor;  // iVoltage LSB = 8.8mV 
        BatteryMonitor<1> battery_monitor;  // iVoltage LSB = 8.8mV 
      #endif
    #else // ESP SaberProp
      template<>  // voltage LSB for LSBINDEX = 0
      float BatteryMonitor<0>::voltageLSB() { return 0.001187; } // 1.1 mV
      BatteryMonitor<0> battery_monitor; 
    #endif    


#endif  // NO_BATTERY_MONITOR

#endif // COMMON_BATTERY_MONITOR_H

