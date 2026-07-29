#ifndef CONFIG_H
#define CONFIG_H

#ifdef ATmega4809
#define LED_R 13
#define LED_G 39
#define LED_B 38
#endif

// Activate this if you want the serial menu to show (temporarily) before activating the IEEE-488 device. 
// This is useful for debugging and configuration, as it gives slightly more freedom than the SCPI interface.
#define SERIAL_MENU_TIMEOUT_S 2

#define DEFAULT_DEBUG_LEVEL 1 

#define DEFAULT_PRIMARY_ADDRESS 5
#define DEFAULT_SECONDARY_ADDRESS 1
#define DEFAULT_EXTENDED_ADDRESS false
#define DEFAULT_T3_TIMEOUT_US 0
#define DEFAULT_T1_DELAY_US 10

#endif // CONFIG_H