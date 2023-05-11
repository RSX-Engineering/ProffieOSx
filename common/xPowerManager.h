/********************************************************************
/*  Power Manager                                                   *
 *  (C) Cosmin PACA @ RSX Engineering. Licensed under GNU GPL v3.   *
 *  fileversion: v1.2 @ 2023/04                                     *
 ********************************************************************
 *  -                                 
 *  -                  
 *  -                                         
 ********************************************************************/

#ifndef XPOWER_MANAHER_H
#define XPOWER_MANAHER_H


        #define PWRMAN_TIMEOUTRES   10000       // timeout resolution, in micros
        #define PWRMAN_MINTIMEOUT   20          // minimum timeout, in millis
        #define PWRMAN_DEFTIMEOUT   1000        // default timeout: 1 second
        #define PWRMAN_AMPTIMEOUT   50         // audio amplifier timeout 
        #define PWRMAN_CPUTIMEOUT   60000       // CPU deep sleep timeout
        //#define PWRMAN_CPUTIMEOUT   10000       // CPU deep sleep timeout 
        #define PWRMAN_SDMOUNTTIMEOUT 5000      // SD mount timeout - allow longer for pre-loop initializations
        #define PWRMAN_STARTON  pwr4_CPU       // Binary map of PDType flags of domains that will turn ON at startup


        // Stop entry type 
        #define PWR_STOPENTRY_WFI               ((uint8_t)0x01)       //Wait For Interruption instruction to enter Stop mode
        #define PWR_STOPENTRY_WFE               ((uint8_t)0x02)       //Wait For Event instruction to enter Stop mode

        // enum describing the resource we are controlling power 
        #define PDType_base uint8_t
        enum PDType : PDType_base { 
            pwr4_none       = 0,
            pwr4_CPU        = 0b00000001,    
            pwr4_SD         = 0b00000010,
            pwr4_Booster    = 0b00000100,
            pwr4_Amplif     = 0b00001000,
            pwr4_Pixel      = 0b00010000,
            pwr4_Charger    = 0b00100000,
            pwr4_Sensor = 200,  // TODO: remove this
            pwr_END = 0b00100000               // update to last power domain << 1, need this as a limit!
        };

        

        enum WKSource {
            wakeUp_none = 0,
            wakeUp_button = 1, // wake up from button
            wakeUp_serial = 2, // wake up from serial
        };



        class xPowerDomain;        
        class xPowerSubscriber;
    #ifdef DIAGNOSE_POWER
        class xPowerManager : public Looper, CommandParser
    #else
        class xPowerManager : public Looper
    #endif // DIAGNOSE_POWER
        
        {   friend class xPowerDomain;
            friend class xPowerSubscriber;
            
        private:
            xPowerDomain* domains;           // linked list of domains
            xPowerSubscriber* subscribers;   // linked list of subscribers
            static WKSource wakeUpSource;   // need this static because we need static ISRs to access it
            PDType_base powerState;         // binary map of all active 'PDType' flags
            uint32_t lastLoopTime;          // time keeper
            
            void Setup() override;          // delayed implementation, see end of file                          
            void Loop() override;           


        public:
            xPowerManager() : Looper(PWRMAN_TIMEOUTRES) {
                powerState = 0;
                domains = NULL;
                lastLoopTime = 0;
            }
            const char* name() override { return "xPowerManager"; }

            // Turn ON power domains 
            bool Activate(PDType_base startUpDomains = PWRMAN_STARTON);            // delayed implementation, see end of file.

        #ifdef DIAGNOSE_POWER
            void Help() override {}
            bool Parse(const char* cmd, const char* arg) override;   // delayed implementation, see end of file.
        #endif // DIAGNOSE_POWER            
        private:    
            // Power wake up ISR - Button
            static void pwrWakeUp_BTN(void* context)
            {
                noInterrupts();         // no make sure that we are not bombarded with noise from btn 
                wakeUpSource = wakeUp_button;
                stm32l4_exti_notify(&stm32l4_exti, g_APinDescription[powerButtonPin].pin,
			      EXTI_CONTROL_DISABLE, NULL, NULL);
                stm32l4_exti_notify(&stm32l4_exti, GPIO_PIN_PA10,
			      EXTI_CONTROL_DISABLE, NULL, NULL);
                interrupts();           // restore int 
            }

            // Power wake up ISR - Serial
            static void pwrWakeUp_SER(void* context)
            {
                wakeUpSource = wakeUp_serial;
            }            

            void CPU_DeepSleep(uint8_t STOPEntry) // __attribute__((optimize("O0"))) 
            {

                wakeUpSource = wakeUp_none;            // reset wake up source 
                armv7m_systick_disable();               // disable systick 

                // set btn and uart RX pin for for wakeup 
                stm32l4_exti_notify(&stm32l4_exti, g_APinDescription[powerButtonPin].pin,
			      EXTI_CONTROL_FALLING_EDGE, &pwrWakeUp_BTN, NULL);    // EXTI_CONTROL_RISING_EDGE
                stm32l4_exti_notify(&stm32l4_exti, GPIO_PIN_PA10,
			      EXTI_CONTROL_RISING_EDGE, &pwrWakeUp_SER, NULL);

                MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STOP0); // Stop 0 mode with Main Regulator
                SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));   // Set SLEEPDEEP bit of Cortex System Control Register

                if(STOPEntry == PWR_STOPENTRY_WFI)      // Select Stop mode entry 
                    __WFI();    // Request Wait For Interrupt
                else {
                    __SEV();    // Request Wait For Event
                    __WFE();
                    __WFE();
                }
                // WAKE up haved occured
                CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk)); // Reset SLEEPDEEP bit of Cortex System Control Register
                
                stm32l4_exti_notify(&stm32l4_exti, g_APinDescription[powerButtonPin].pin,
			      EXTI_CONTROL_DISABLE, NULL, NULL);
                stm32l4_exti_notify(&stm32l4_exti, GPIO_PIN_PA10,
			      EXTI_CONTROL_DISABLE, NULL, NULL);

                stm32l4_system_initialize(_SYSTEM_CORE_CLOCK_, _SYSTEM_CORE_CLOCK_/2, _SYSTEM_CORE_CLOCK_/2, 0, STM32L4_CONFIG_HSECLK, STM32L4_CONFIG_SYSOPT);
                armv7m_systick_enable(); 

            }





        } powerman;     // global object!



        // POWER DOMAINS
        // ==================================================================================
        // Power domain objects must inherit from this class to get registered. 
        class xPowerDomain {
            friend class xPowerManager;
        private:
            uint32_t countdownTimer;
        protected:
            // PDType_base* PowerState() {return &powerman.powerState; }
        #ifdef DIAGNOSE_POWER
            void PrintPowerState(bool newState) {   
                // STDOUT.print(millis()/1000.f); STDOUT.print("[s]: SetPower("); STDOUT.print(name()); STDOUT.print(") to "); 
                // if (newState) { 
                //     STDOUT.print("ON, will expire in "); 
                //     STDOUT.print(countdownTimer/1000.f); 
                //     STDOUT.println(" [s].");
                // }
                // else STDOUT.println("OFF");
                if (newState) { 
                    STDOUT.print("~"); 
                }
                else STDOUT.print("_");

            }
        #endif // DIAGNOSE_POWER
            virtual void Setup() {}         // Override this to provide domain-specific setup             
        public:              
            xPowerDomain* next;
            xPowerDomain() { // Constructor lists domain in powerman.domains
                countdownTimer = 0;         // timer not started
                next = powerman.domains;    // link to 'domains' list
                powerman.domains = this;
            }                      

            virtual PDType id()  = 0;           // Binary flag, unique for each xPowerDomain object
            virtual const char* name()  = 0;    // Domain name, for terminal reporting only
            virtual void SetPower(bool newState) = 0;       // Switches power domain ON/OFF
            virtual uint32_t timeout() { return PWRMAN_DEFTIMEOUT; }  // Default domain timeout, in milliseconds. Override to change.

            // Someone is requesting power, update timeout to specified millis (or domain-specific if unspecified)
            void ResetTimeout(uint32_t timeout_=0) {                            
                if (!timeout_) timeout_ = timeout();        // domain-specific timeout if not specified
                if (timeout_ < PWRMAN_MINTIMEOUT) timeout_ = PWRMAN_MINTIMEOUT; // keep minimum
                if (countdownTimer < timeout_) countdownTimer = timeout_;    // reset timeout
            } 

            // Check expire time and turn power off if needed, return true if timed-out. Optionally specify measured time between successive calls, in millis
            bool CheckTimeout(uint32_t loopTime=PWRMAN_TIMEOUTRES) {
                if (!countdownTimer) return false;      // domain not timed
                if (countdownTimer <= loopTime) {       // timed out, reset timer
                    countdownTimer = 0;
                    return true; 
                }    
                countdownTimer -= loopTime;             // not timed out, update timer
                return false;                 
            }               

        };

        class PowerDomain_Pixel : public xPowerDomain {
        public:            
            PowerDomain_Pixel() : xPowerDomain() {}    // Link domain to powerDomains
            PDType id() override { return pwr4_Pixel; } // define ID
            const char* name() override { return "PIX"; }  // define name
            void Setup() override {
                stm32l4_gpio_pin_configure(GPIO_PIN_PB2, GPIO_MODE_ANALOG | GPIO_OTYPE_PUSHPULL | GPIO_OSPEED_LOW | GPIO_PUPD_PULLDOWN);  // Power OFF
                //pinMode(GPIO_PIN_PB2, INPUT_PULLUP);
            }
            void SetPower(bool newState) {   // Switches power domain ON/OFF
                if (newState) 
                    stm32l4_gpio_pin_configure(GPIO_PIN_PB2, GPIO_MODE_OUTPUT | GPIO_OTYPE_PUSHPULL | GPIO_OSPEED_LOW | GPIO_PUPD_PULLDOWN);    // Power ON
                else
                    stm32l4_gpio_pin_configure(GPIO_PIN_PB2, GPIO_MODE_ANALOG | GPIO_OTYPE_PUSHPULL | GPIO_OSPEED_LOW | GPIO_PUPD_PULLDOWN);  // Power OFF
            #ifdef DIAGNOSE_POWER
                PrintPowerState(newState);
            #endif // DIAGNOSE_POWER
                
            }
        }xpd_pixel;  // one object must exist

        class PowerDomain_Amplif : public xPowerDomain {
        public:            
            PowerDomain_Amplif() : xPowerDomain() {}    // Link domain to powerDomains
            PDType id() override { return pwr4_Amplif; } // define ID
            const char* name() override { return "AMP"; }  // define name
             uint32_t timeout() override { return PWRMAN_AMPTIMEOUT; }  // Override timeout
            
            void SetPower(bool newState) {   // Switches power domain ON/OFF

                if (newState) { // Power ON
                    digitalWrite(amplifierPin, HIGH); 
                  #ifdef ULTRA_PROFFIE
                    //stm32l4_gpio_pin_configure(amplifierPin, GPIO_MODE_OUTPUT | GPIO_OTYPE_PUSHPULL | GPIO_OSPEED_LOW | GPIO_PUPD_PULLDOWN);
                  #else
                    pinMode(amplifierPin, OUTPUT);
                  #endif                    
                }
                else // Power OFF
                  #ifndef ULTRA_PROFFIE
                    pinMode(amplifierPin, INPUT_ANALOG); // Let the pull-down do the work
                  #else 
                    digitalWrite(amplifierPin, LOW); // turn the amplifier off                
                  #endif                
                                                
               #ifdef DIAGNOSE_POWER
                PrintPowerState(newState);
               #endif // DIAGNOSE_POWER
            }
        } xpd_amp;    // one object must exist


        class PowerDomain_Booster : public xPowerDomain {
        public:            
            PowerDomain_Booster() : xPowerDomain() {}    // Link domain to powerDomains
            PDType id() override { return pwr4_Booster; } // define ID
            const char* name() override { return "BST"; }  // define name
            

            
            void SetPower(bool newState) {   // Switches power domain ON/OFF                
                if (newState) { // Power ON
                    pinMode(boosterPin, OUTPUT);
                    digitalWrite(boosterPin, HIGH);
                }
                else { // Power OFF
                    digitalWrite(boosterPin, LOW); // turn the booster off
                }
            #ifdef DIAGNOSE_POWER
                PrintPowerState(newState);
            #endif // DIAGNOSE_POWER                
            }
        } xpd_boost;    // one object must exist

        class PowerDomain_SD : public xPowerDomain {
        public:            
            PowerDomain_SD() : xPowerDomain() {}    // Link domain to powerDomains
            PDType id() override { return pwr4_SD; } // define ID
            const char* name() override { return "SD"; }  // define name
            // uint32_t timeout() override { return PWRMAN_SDTIMEOUT; }  // Domain timeout, in milliseconds.
            
            void SetPower(bool newState) {   // Switches power domain ON/OFF   
                #if DOSFS_SDCARD == 1 && DOSFS_SFLASH == 0
                    if (newState) { // Power ON
                        stm32l4_gpio_pin_configure(PIN_SPI_SD_POWER, (GPIO_PUPD_NONE | GPIO_OSPEED_HIGH | GPIO_OTYPE_PUSHPULL | GPIO_MODE_OUTPUT));
                        stm32l4_gpio_pin_write(PIN_SPI_SD_POWER, 0);
                    }                    
                    else { // Power OFF
                        stm32l4_gpio_pin_configure(PIN_SPI_SD_POWER, (GPIO_PUPD_NONE | GPIO_OSPEED_HIGH | GPIO_OTYPE_PUSHPULL | GPIO_MODE_OUTPUT));
                        stm32l4_gpio_pin_write(PIN_SPI_SD_POWER, 1);                
                    }
                #endif
            #ifdef DIAGNOSE_POWER
                PrintPowerState(newState);
            #endif // DIAGNOSE_POWER
            }
        } xpd_sd;    // one object must exist


        class PowerDomain_CPU : public xPowerDomain {
        public:            
            PowerDomain_CPU() : xPowerDomain() {}    // Link domain to powerDomains
            PDType id() override { return pwr4_CPU; } // define ID
            const char* name() override { return "CPU"; }  // define name
            uint32_t timeout() override { return PWRMAN_CPUTIMEOUT; }  // Override timeout
            
            void SetPower(bool newState) {  // Nothing to do, CPU sleep mode is handled by xPowerManager
                // SaberBase::motion_active_ = newState;   // activate / deactivate motion
                // gyroscope->Loop();                      // run loop once to allow sensor state machine to register the change
            #ifdef DIAGNOSE_POWER
                PrintPowerState(newState);
            #endif // DIAGNOSE_POWER                
            }   
        } xpd_cpu;    // one object must exist




        // POWER SUBSCRIBERS
        // ==================================================================================
        // Power subscriber objects must inherit from this class 
        class xPowerSubscriber {
        friend class xPowerManager;
        private:
            PDType_base subscribedDomains;  // binary map
            xPowerSubscriber* next;
        protected:
            virtual const char* name() {}       // Subscriber's name, defined by child 
            virtual bool HoldPower() { return false; }   // Override to pause timeout
            virtual void PwrOn_Callback() {}          // Subscriber-specific code to be executed when subscriber power goes ON
            virtual void PwrOff_Callback() {}         // Subscriber-specific code to be executed when subscriber power goes OFF

        public:    
            // Constructor lists subscriber in powerman.subscribers
            xPowerSubscriber(PDType_base domains) { 
                if (!domains) return;
                subscribedDomains = domains;        // mark domains
                next = powerman.subscribers;        // link to subscribers list
                powerman.subscribers = this;
            }                      
                                 
            // Check if all the subscribed domains are active
            bool IsOn() {  return ((powerman.powerState & subscribedDomains) == subscribedDomains); }

            // Keep power ON until timeout on all subscribed domains. Turns domains ON if needed.
            // Returns true if subscriber went on just now
            // If specified, timeout_ must point to a vector of uint32_t with the tineout value in millis for each subscribed domain, e.g. 'RequestPower(&(const uint32_t t=100));' or 'RequestPower(&(const uint32_t[] t={100,200}));'
            // If not specified, domain-specific timeout will be applied to each subscribed domain
            bool RequestPower(uint32_t* timeout_ = 0) {
                bool retVal = false;
                if (!powerman.domains) return false;        // no subscribed domain
                
                // 1. Reset timeout & power ON if needed
                for (xPowerDomain *pd = powerman.domains; pd; pd = pd->next) {
                    if (pd->id() & subscribedDomains) { // domain is subscribed, will request power
                        if (!timeout_) pd->ResetTimeout();    // use domain timeout
                        else pd->ResetTimeout(*timeout_++);    // use specified timeout
                        if (!(powerman.powerState & pd->id())) {  // turn domain on if it's off
                            pd->SetPower(true);         
                            powerman.powerState |= pd->id();     // mark domain is on
                            #ifdef DIAGNOSE_POWER 
                                STDOUT.println(powerman.powerState);
                            #endif
                            retVal = true;                      // mark subscriber turned on
                        }
                    }
                }

                // 2. Run On_Callback if the domain just powered on
                if (retVal) PwrOn_Callback();

                return retVal;

            }


        };

        WKSource xPowerManager::wakeUpSource = wakeUp_none;

        
            
        // xPowerManager delayed implementations ------------
        
        void xPowerManager::Setup() { 
            if (domains)
            for (xPowerDomain *pd = domains; pd; pd = pd->next) 
                pd->Setup();            // setup
            Activate();                 // turn on startup power domains 
        }      
        
            // __attribute__((optimize("O0"))) 
        void xPowerManager::Loop()  {
            if (!domains) return;

            uint32_t timeNow = millis();
            uint32_t loopTime = timeNow - lastLoopTime;

            // if (loopTime > PWRMAN_AMPTIMEOUT) { STDOUT.print("-- Loop "); STDOUT.println(loopTime); }
            
            lastLoopTime = timeNow;
            if (loopTime >= PWRMAN_MINTIMEOUT) {
                #ifdef DIAGNOSE_POWER                                     
                    STDOUT.print(".");
                #endif
                return;     // Don't run if loop took longer than normal, allow the looper to cycle once in order to request power if still needed
            }


            // 1. Identify domains that should hold power, so we won't check timeout on those
            PDType_base domainsToCheck = 0;     
            if (subscribers)
                for (xPowerSubscriber *ps = subscribers; ps; ps = ps->next) 
                    if (ps->HoldPower()) domainsToCheck |= ps->subscribedDomains; // set bit if domain is on hold                           
            domainsToCheck = ~domainsToCheck;    // invert, so now we have a bit set for each domain NOT on hold

            // 2. Check timeout on domains active and not on hold, but don't turn them off, just mark them.
            PDType_base nextPowerState = powerState;
            for (xPowerDomain *pd = domains; pd; pd = pd->next) { 
                if (pd->id() & powerState & domainsToCheck) 
                    if (pd->CheckTimeout(loopTime)) {
                        nextPowerState &= ~pd->id();         // domain timed out, clear flag in next state
                    }
            } 
            if (nextPowerState == powerState) return;       // nothing else to do

            // 3. Run power off callbacks if needed, before actually turning the power off
            if (subscribers )
                for (xPowerSubscriber *ps = subscribers; ps; ps = ps->next) 
                    // if((ps->subscribedDomains & powerState == ps->subscribedDomains) && 
                    //    (ps->subscribedDomains & nextPowerState != ps->subscribedDomains)) // at least one subscribed domain will turn off
                    //         ps->PwrOff_Callback();
                    {
                        PDType_base tmp = ps->subscribedDomains & powerState;
                        if (tmp == ps->subscribedDomains) {
                            tmp = ps->subscribedDomains & nextPowerState;
                            if (tmp != ps->subscribedDomains)
                                ps->PwrOff_Callback();
                        }

                    }

            // 4. Turn off domains marked at (2)           
            for (xPowerDomain *pd = domains; pd; pd = pd->next) { 
                if ((pd->id() & powerState) && (pd->id() & ~nextPowerState)) {
                        pd->SetPower(false);                     // turn power domain off
                        powerState &= ~pd->id();                 // clear flag 
                        #ifdef DIAGNOSE_POWER 
                            STDOUT.println(powerState);
                        #endif

                    }
            }                    

            // 5. Go to deep sleep if no domain is active
            if (powerState) return  // nothing else to do if domains are still active
            STDOUT.flushTx();
            #if defined(BOARDTYPE_LITE)     
                #ifdef DIAGNOSE_POWER
                    STDOUT.print ("Publishing content in "); STDOUT.print(OFFLINE_FILE); STDOUT.print("... ");
                    if (PublishContent(OFFLINE_FILE)) STDOUT.println("Success.");
                    else STDOUT.println("FAILED!");
                #else // DIAGNOSE_POWER
                    PublishContent(OFFLINE_FILE); 
                #endif // DIAGNOSE_POWER
                LSFS::End();     // make sure that card is unmounted 
                xpd_sd.SetPower(false);     // SD power was turned on by PublishContent()
                powerState = 0;             // clear all flags
            #endif // BOARDTYPE_LITE
            STDOUT.print(timeNow/1000.f); STDOUT.println("[s]: All power domains off, entering DEEP SLEEP."); 
            STDOUT.println("..."); STDOUT.println("..."); STDOUT.println("..."); STDOUT.println("");
            STDOUT.flushTx();
            CPU_DeepSleep(PWR_STOPENTRY_WFI);       // Enter & exit STOP0 mode
            // ................
            // ... sleeeeep ...
            // ................                        
            STDOUT.print("WAKE-UP! Source: ");
            switch(wakeUpSource) {
                case wakeUp_button:     STDOUT.println("Button.");
                                        break;                

                case wakeUp_serial:     STDOUT.flushRx();
                                        STDOUT.println("Terminal. "); STDOUT.println("= command disregarded = ");                                         
                                        break;

                default:                STDOUT.println("unknown!");
                
            }           
            Activate();     // turn on startup domains

        }

        // Turn ON specified power domains. If called without parameters, will turn on domains defined by PWRMAN_STARTON
        // Return true if any domain went on
        bool xPowerManager::Activate(PDType_base startUpDomains) {
            #ifdef DIAGNOSE_POWER            
                STDOUT.print("Activate domains: "); STDOUT.println(startUpDomains);
            #endif
            if (!domains) return false;
            // 1. Turn on domains that need to be on
            bool retVal = false;    // assume no domain goes on
            for (xPowerDomain *pd = domains; pd; pd = pd->next) { 
                if((pd->id() & startUpDomains) && !(pd->id() & powerState)) { // Domain should be on and it itsn't, turn it ON now:
                    // pd->SetPower(true);
                    pd->ResetTimeout();
                    powerState |= pd->id();     
                    #ifdef DIAGNOSE_POWER                     
                        STDOUT.println(powerState);
                    #endif
                    pd->SetPower(true);
                    retVal = true;
                }
            }
            // 2. Call subscriber callbacks if any just turned on
            if (!subscribers || !retVal) return retVal;     // no subscribers or no domain turned on
            for (xPowerSubscriber *ps = subscribers; ps; ps = ps->next) 
                if (ps->IsOn()) ps->PwrOn_Callback();
            return retVal;
        }           


    // class powerSub1 : public xPowerSubscriber {
    // public:
    //     powerSub1(PDType_base dom) : xPowerSubscriber(dom) {}
    //     const char* name() { return "TestS1"; }       // Subscriber's name, defined by child 
    //     void PwrOn_Callback() { STDOUT.print(millis()/1000.f); STDOUT.println("[s]: Test subscriber 1 (BST) goes ON."); }         
    //     void PwrOff_Callback() { STDOUT.print("["); STDOUT.print(millis()/1000.f); STDOUT.println("] Test subscriber 1 (BST) goes OFF."); }       
        
    //     bool keepOn = false;
        
    //     bool HoldPower() override { // Override to pause timeout
    //         return keepOn; 
    //     }   

    // };
    // powerSub1 testSubscriber1(pwr4_Booster);

    #ifdef DIAGNOSE_POWER
        bool xPowerManager::Parse(const char* cmd, const char* arg) {

            // "pwr-domains" - report status of all power domain objects
            if (!strcmp(cmd, "pwr-domains")) {
                uint32_t timeNow = millis();
                STDOUT.print("Power domains @ "); STDOUT.print(timeNow/1000.f); STDOUT.println(" [s]:");
                if (!domains) return true;
                for (xPowerDomain *pd = domains; pd; pd = pd->next) {
                    STDOUT.print(" * "); STDOUT.print(pd->name()); STDOUT.print(" @ "); STDOUT.print(pd->timeout()); STDOUT.print(" [ms] is ");                    
                    if (pd->id() & powerState) {
                        STDOUT.print("ON - set to expire in "); STDOUT.print(pd->countdownTimer/1000.f); STDOUT.println(" [s].");
                    }
                    else {
                        STDOUT.println("OFF.");
                    }
                }
                return true;
            } 

            // "pwr-dom-request <domain_name>, <timeout_millis>" - request power for a specified domain, with a specified timeout
            if (!strcmp(cmd, "pwr-dom-request")) {
                uint32_t timeNow = millis();
                char argh[20];
                strcpy(argh, arg);
                char* token = strtok(argh, ",");
                if (!domains) return true;
                for (xPowerDomain *pd = domains; pd; pd = pd->next) {
                    if (!strcmp(token, pd->name())) {   // found domain with this name
                        token = strtok(NULL, ",");
                        uint32_t requested_timeout = atoi(token);    
                        if (requested_timeout < PWRMAN_MINTIMEOUT) requested_timeout = PWRMAN_MINTIMEOUT;
                        pd->ResetTimeout(requested_timeout);    // set domain timeout
                        if (!(pd->id() & powerman.powerState)) {  
                            pd->SetPower(true);         // turn power on
                            powerState |= pd->id();     // mark domain is on
                            #ifdef DIAGNOSE_POWER 
                                STDOUT.println(powerState);
                            #endif

                        }
                        STDOUT.print("Power requested for domain '"); STDOUT.print(pd->name()); STDOUT.println("'.");
                        return true;
                    }
                }
                STDOUT.println("Unknown domain.");
                return true;
            } 

            // "pwr-dom-off <domain_name>" - turn off a specified domain
            if (!strcmp(cmd, "pwr-dom-off")) {
                uint32_t timeNow = millis();
                if (!domains) return true;
                for (xPowerDomain *pd = domains; pd; pd = pd->next) {
                    if (!strcmp(arg, pd->name())) {   // found domain with this name
                        if (pd->id() & powerState) {  
                            pd->SetPower(false);         // turn power off
                            powerState &= ~pd->id();     // mark domain is off
                            #ifdef DIAGNOSE_POWER 
                                STDOUT.println(powerState);
                            #endif                            
                            pd->countdownTimer = 0;     
                        }
                        STDOUT.print("Domain "); STDOUT.print(pd->name()); STDOUT.println(" turned OFF."); 
                        return true;
                    }
                }
                STDOUT.println("Unknown domain.");
                return true;
            } 

            // "pwr-subs" - report status of all power subscriber objects
            if (!strcmp(cmd, "pwr-subs")) {
                uint32_t timeNow = millis();
                STDOUT.print("Power subscribers @ "); STDOUT.print(timeNow/1000.f); STDOUT.println(" [s]:");
                if (!subscribers) return true;                
                for (xPowerSubscriber *ps = subscribers; ps; ps = ps->next) {
                    STDOUT.print(" * "); STDOUT.print(ps->name()); STDOUT.print(" {dom="); STDOUT.print(ps->subscribedDomains); STDOUT.print("} is ");                    
                    if (ps->IsOn()) STDOUT.print("ON");
                    else STDOUT.print("Off");
                    STDOUT.print(", PowerHold = "); STDOUT.println(ps->HoldPower());
                }
                return true;
            }           

            // "pwr-sub-request <subscriber_name>" - request power for a subscriber, at domain-specific timeouts
            if (!strcmp(cmd, "pwr-sub-request")) {
               uint32_t timeNow = millis();
                if (!subscribers) return true;
                PDType_base mask=0;
                for (xPowerSubscriber *ps = subscribers; ps; ps = ps->next) {
                    if (!strcmp(arg, ps->name())) {   // found subscriber with this name
                        STDOUT.print(ps->name()); STDOUT.println(" requested power."); 
                        ps->RequestPower();                        
                        return true;
                    }
                }
                STDOUT.println("Unknown subscriber.");
                return true;
            }

            if (!strcmp(cmd, "deepsleep")) {
                STDOUT.println("Deep sleep...");
                STDOUT.flushTx();
                CPU_DeepSleep(PWR_STOPENTRY_WFI);       // Enter & exit STOP0 mode
                STDOUT.println("Wake up!");
                return true;
            }


            return false;
        }        
    #endif // DIAGNOSE_POWER
       
        // ------------ xPowerManager delayed implementations 

    // // test subscriber 
    // class powerSub1 : public xPowerSubscriber {
    // public:
    //     powerSub1(PDType_base dom) : xPowerSubscriber(dom) {}
    //     const char* name() { return "TestS1"; }       // Subscriber's name, defined by child 
    //     void PwrOn_Callback() { STDOUT.print(millis()/1000.f); STDOUT.println("[s]: Test subscriber 1 (BST) goes ON."); }         
    //     void PwrOff_Callback() { STDOUT.print("["); STDOUT.print(millis()/1000.f); STDOUT.println("] Test subscriber 1 (BST) goes OFF."); }         
    // };
    // powerSub1 testSubscriber1(pwr4_Booster);

    // class powerSub2 : public xPowerSubscriber {
    // public:
    //     powerSub2(PDType_base dom) : xPowerSubscriber(dom) {}
    //     const char* name() { return "TestS2"; }       // Subscriber's name, defined by child 
    //     void PwrOn_Callback() { STDOUT.print(millis()/1000.f); STDOUT.println("[s]: Test subscriber 2 (SD+CPU) goes ON."); }         
    //     void PwrOff_Callback() { STDOUT.print(millis()/1000.f); STDOUT.println("[s]: Test subscriber 2 (SD+CPU) goes OFF."); }       
    // };
    // powerSub2 testSubscriber2(pwr4_SD | pwr4_CPU);


    // #endif

#endif