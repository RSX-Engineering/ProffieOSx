#ifndef XPROD_SERIAL_H
#define XPROD_SERIAL_H

    #if defined(SABERPROP)

        #define XPROD_OTP_ADD_START 0x1FFF7000
        #define XPROD_OTP_ADD_END   0x1FFF73FF

        #ifdef ARDUINO_ARCH_ESP32
        #include "esp_efuse.h"
        #endif
        class HwID 
        {

            public:

            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
            void printHWSN(FileReader* fw)
            {
                uint8_t id, audio,charger, sns , batProt, cpu;
                char namePrint[18];
                char shorname[6], storage[6];
                shorname[5] = 0; namePrint[17] = 0; storage[5] = 0;

                id= (hwID.hw_version & 0xFF000000) >> 24;
                audio = (hwID.hw_version & 0x00F00000) >> 20;
                charger = (hwID.hw_version & 0x000E0000) >> 17;  
                sns = (hwID.hw_version & 0x0001C000) >> 14;
                batProt = (hwID.hw_version & 0x00002000) >> 13;
                cpu = (hwID.hw_version & 0x00001C00) >> 12;
                
                id = 2;     // TODO delete this
                switch(id)
                {
                    case 0: strcpy(namePrint, "UltraProffie Zero"); strcpy(storage,"FLASH"); shorname[0] = 'P'; shorname[1]='Z';break;
                    case 1: strcpy(namePrint, "UltraProffie Lite"); strcpy(storage,"SD");shorname[0] = 'P'; shorname[1]='L';break;
                    case 2: strcpy(namePrint, "SaberProp"); strcpy(storage,"SD");shorname[0] = 'S'; shorname[1]='P';break;
                    case 3: strcpy(namePrint, "SaberProp Lite"); strcpy(storage,"SD");shorname[0] = 'S'; shorname[1]='L';break;
                    default:strcpy(namePrint, "UNKNOWN");strcpy(storage,"UNK");shorname[0]='N'; shorname[1]=' '; break;
                }
                switch(audio)
                {
                    case 0: audio = 2; shorname[2]='2';break;
                    case 1: audio = 3; shorname[2]='3';break;
                    default: shorname[2]='N';break;
                }
                switch(charger)
                {
                    case 0: shorname[3]='0';break;
                    case 1:  shorname[3]='1';break;
                    default: shorname[3]='N';break;
                }
                switch(sns)
                {
                    case 0: shorname[4]='L';break;
                    default: shorname[4]='N';break;
                }
                if(!fw) { 
                    STDOUT.print("ID: "); STDOUT.print(namePrint);STDOUT.print(" ");STDOUT.println(shorname);
                    STDOUT.print("Audio: ");STDOUT.print(audio);STDOUT.println("W");
                    STDOUT.print("Charger: "); STDOUT.println(charger == 1 ? "1A" : "NONE");  
                    STDOUT.print("Sensor: "); STDOUT.println(sns == 0 ? "LSM": "UNKNOWN");
                    STDOUT.print("Battery protection: ");STDOUT.println(batProt == 1 ? "On" : "Off");
                    STDOUT.print("Storage: ");STDOUT.println(storage);
                    STDOUT.print("CPU: "); //STDOUT.println(cpu == 0 ? "STM32L431CC" :"Unknown");
                    #ifdef ARDUINO_ARCH_STM32L4
                        STDOUT.println("STM32L431CC");
                    #else
                        STDOUT.println("ESP32-S3");
                    #endif                    
                    STDOUT.print("SerialNumber: ");STDOUT.println(hwID.hw_SerialNumber); 
                    char longString[9];
                    sprintf(longString, "%08X", *(((uint32_t*)&hwID.hw_hex)+1));
                    STDOUT.print("HexString: "); STDOUT.print(longString); 
                    sprintf(longString, "%08X", (uint32_t)hwID.hw_hex);
                    STDOUT.println(longString); 
                }
                else {
                    char buffer[30];
                    sprintf(buffer, "%d", audio);
                    fw->Write("ID: "); fw->Write(namePrint); fw->Write(" ");fw->Write(shorname);fw->Write("\n");
                    fw->Write("Audio: ");fw->Write(buffer); fw->Write("W\n");
                    fw->Write("Charger: "); fw->Write(charger == 1 ? "1A\n" : "NONE\n");  
                    fw->Write("Sensor: "); fw->Write(sns == 0 ? "LSM\n": "UNKNOWN\n");
                    fw->Write("Battery protection: ");fw->Write(batProt == 1 ? "On\n" : "Off\n");
                    fw->Write("Storage: ");fw->Write(storage);fw->Write("\n");
                    fw->Write("CPU: "); // fw->Write(cpu == 0 ? "STM32L431CC\n" :"Unknown\n");
                    #ifdef ARDUINO_ARCH_STM32L4
                         fw->Write("STM32L431CC");
                    #else
                         fw->Write("ESP32-S3");
                    #endif                    
                    sprintf(buffer, "%lu", (unsigned long)hwID.hw_SerialNumber);
                    fw->Write("SerialNumber: ");fw->Write(buffer);fw->Write("\n");  
                    char longString[9];
                    sprintf(longString, "%08X", *(((uint32_t*)&hwID.hw_hex)+1));
                    fw->Write("HexString: "); 
                    fw->Write(longString); 
                    sprintf(longString, "%08X", (uint32_t)hwID.hw_hex);
                    fw->Write(longString);    
                    fw->Write("\n");    
                }
            }
            
            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
            HwID()
            {   
                #if defined(ARDUINO_ARCH_STM32L4) || defined(ARDUINO_ARCH_STM32U5) 
                currentAddr = XPROD_OTP_ADD_START;
                #else 
                currentAddr = EFUSE_BLK3;
                #endif
                 if(!readOTPValidValues(&hwID))
                 {
                    hwID.hw_SerialNumber = 0xFFFFFFFF;
                    hwID.hw_version = 0xFFFFFFFF;
                    hwID.hw_hex = 0xFFFFFFFFFFFFFFFF;
                 }
            }

            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
            uint32_t xGetSerial()
            {
                return hwID.hw_SerialNumber;
            }

            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
            uint32_t xGetHwId()
            {
                return hwID.hw_version;
            }

            private:

            typedef struct 
            {
                uint32_t hw_SerialNumber;
                uint32_t hw_version;
                uint64_t hw_hex;
                
            } __attribute__((packed))  hw_ids;

            uint32_t currentAddr;
            hw_ids hwID;

            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
           #if defined(ARDUINO_ARCH_STM32L4) || defined(ARDUINO_ARCH_STM32U5)
            bool readOTPValues(uint32_t address, hw_ids *hw)
            {
                if(address < XPROD_OTP_ADD_START || address > XPROD_OTP_ADD_END)
                    return false;   // invalid space address 
                // if(address % 8) 
                if(address % 16) 
                    return false;   // invalid offset 
                hw->hw_version = *(uint32_t*)(address);
                hw->hw_SerialNumber = *(uint32_t*)(address +  4);
                hw->hw_hex = *(uint64_t*)(address +  8);
                return true;
            }
            #elif ARDUINO_ARCH_ESP32
            bool readOTPValues(esp_efuse_block_t address, hw_ids *hw)
            {
                if(address < EFUSE_BLK3 || address > EFUSE_BLK9)
                    return false;   // invalid space address 
                // if(address % 8) 
                hw->hw_version = esp_efuse_read_reg(address, 0);
                hw->hw_SerialNumber =  esp_efuse_read_reg(address, 1);
                hw->hw_hex = ((uint64_t)esp_efuse_read_reg(address, 2) << 32) | ((uint64_t)esp_efuse_read_reg(address, 3));
                return true;
            }
            #endif

            /*  @brief  :   
            *   @param  :
            *   @retval :
            */
            bool readOTPValidValues(hw_ids *hw)
            {
                #ifdef ARDUINO_ARCH_ESP32   // ESP architecture

                hw_ids currentID;
                hw_ids nextID;
                for(currentAddr = EFUSE_BLK3; currentAddr <= EFUSE_BLK9; currentAddr += 1)
                {
                    if (readOTPValues((esp_efuse_block_t)currentAddr, &currentID) && readOTPValues((esp_efuse_block_t)(currentAddr+1), &nextID))
                    {
                        if(currentID.hw_version != 0x00000000 && currentID.hw_SerialNumber != 0x00000000) 
                        {   // found data, check if it's latest 
                            if ((nextID.hw_version == 0x00000000 && nextID.hw_SerialNumber == 0x00000000) || currentAddr == EFUSE_BLK9)
                            {   // we have a valid IDS so sore and break;
                                *hw = currentID;
                                return true;
                            }
                        }
                    }
                }
                #else 
                hw_ids currentID;
                hw_ids nextID;
                for(currentAddr = XPROD_OTP_ADD_START; currentAddr <= XPROD_OTP_ADD_END-16; currentAddr += 16)
                {
                    if (readOTPValues(currentAddr, &currentID) && readOTPValues(currentAddr+16, &nextID))
                    {
                        if(currentID.hw_version != 0xFFFFFFFF && currentID.hw_SerialNumber != 0xFFFFFFFF) 
                        {   // found data, check if it's latest 
                            if ((nextID.hw_version == 0xFFFFFFFF && nextID.hw_SerialNumber == 0xFFFFFFFF) || currentAddr == XPROD_OTP_ADD_END-16)
                            {   // we have a valid IDS so sore and break;
                                *hw = currentID;
                                return true;
                            }
                        }
                    }
                }
                #endif
                return false;
            }

        };
    HwID PROFFIE_HDID;
    #endif  // end of ULTRA_PROFFIE

#endif  // end of XPRODSERIAL