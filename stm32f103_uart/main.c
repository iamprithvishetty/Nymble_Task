/*
    ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "ch.h"
#include "hal.h"

#include "shell.h"
#include "chprintf.h"

#include "usbcfg.h"

#define DEBUG false

/*===========================================================================*/
/* FLASH Macro Definitions                                                   */
/*===========================================================================*/

#define PAGE_START 127
#define PAGE_END   127

/*===========================================================================*/
/* USART Macro Definitions                                                   */
/*===========================================================================*/

#define START_BYTE 0x8D
#define END_BYTE   0x8F
#define ACK_BYTE   0x90

/*===========================================================================*/
/* USART SERIAL driver related.                                              */
/*===========================================================================*/

static const SerialConfig sd1_config =
{
  2400, // BAUD RATE
  0,
  USART_CR2_STOP1_BITS,
  0
};

/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

/*
 * CRC Algorithm Implementation
 * arg1 : pointer to data buffer
 * arg2 : length of data buffer
 * arg3 : crc generator polynomial value
 * return : crc value
 */
uint8_t gencrc(uint8_t *data, uint8_t len, uint8_t generator)
{
    // CRC Initial Value
    uint8_t crc = 0x00;

    for (uint8_t i = 0; i < len; i++) {
        // CRC byte exored with next byte in the sequence
        crc ^= data[i];

        // Main CRC Algorithm
        for (uint8_t j = 0; j < 8; j++) {
            // Check if crc has 1 in MSB if not then left shift
            if ((crc & 0x80) != 0)
                // Exor with generator after left shifting 1
                crc = (uint8_t)((crc << 1) ^ generator);
            else
                crc <<= 1;
        }
    }

    return crc;
}

/*
 * Blinker thread, times are in milliseconds. (To keep a check if code is working)
 */
static THD_WORKING_AREA(waThreadBlink, 128);
static __attribute__((noreturn)) THD_FUNCTION(ThreadBlink, arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  while (true) {
    // Clear PC13 Pin
    palClearPad(GPIOC, 13);
    // Wait for 1s
    chThdSleepMilliseconds(1000);
    // Set PC13 Pin
    palSetPad(GPIOC, 13);
    // Wait for 1s
    chThdSleepMilliseconds(1000);
  }
}

/*
 * Application entry point.
 */
int main(void) {

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /*
   * Initializes a serial-over-USB CDC driver.
   */
  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);

  /*
   * Activates the USART Serial Driver
   */
  sdStart(&SD1, &sd1_config);

  /*
   * Activates the USB driver and then the USB bus pull-up on D+.
   * Note, a delay is inserted in order to not have to disconnect the cable
   * after a reset.
   */
  usbDisconnectBus(serusbcfg.usbp);
  chThdSleepMilliseconds(1500);
  usbStart(serusbcfg.usbp, &usbcfg);
  usbConnectBus(serusbcfg.usbp);

  /*
   * Shell manager initialization.
   */
  shellInit();

  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThreadBlink, sizeof(waThreadBlink), NORMALPRIO, ThreadBlink, NULL);

  /*
   * Normal main() thread activity, spawning shells.
   */

  /*
   * Erase Start Page to End Page of STM to store incoming data
   */

  uint8_t start_page = PAGE_START;
  uint8_t end_page = PAGE_END;

  // Perform Unlock Sequence
  efl_lld_start(&EFLD1);
  
  // Erase the pages
  for(uint8_t start = start_page; start <= end_page; start++){
      // 0x400 represents 1kb in hex
      efl_lld_start_erase(&EFLD1, 0x400*start_page);
  }

  // Perform Lock 
  efl_lld_stop(&EFLD1);

  // To keep a track oof where the new data is to be stored in flash memory
  uint16_t current_position_flash = 0;

  while (true) {

    // Read the uart line and check if data is available
    uint8_t received_char = sdGet(&SD1);

    // If data is available
    if(received_char!=0){
      
      // If Start Byte is detected then incoming message detected
      if(received_char == START_BYTE){
        
        // Store the data length of the incoming data
        uint8_t data_len = sdGet(&SD1);

        // Check if data_len is not equal to zero only then do further processing
        if(data_len != 0){
          
          // Dynamically allocated memory for incoming data along with crc of length data_len+1(crc byte)
          uint8_t *data_incoming = (uint8_t *)malloc(sizeof(uint8_t)*(data_len+1));

          // Store the incoming data in a dynamically allocated array
          for(uint8_t start=0; start<=data_len; start++){
            data_incoming[start] = sdGet(&SD1);
          }

          // If End Byte is detected then do further processing
          if(sdGet(&SD1)==END_BYTE){

            // Check if CRC is correct only then store the data in flash and send Acknowledge Byte
            if(!gencrc(data_incoming, data_len+1, 0x31)){
              
              //Perform Unlock Sequence
              efl_lld_start(&EFLD1);
              // Write the data onto flash memory
              efl_lld_program(&EFLD1, 0x400*start_page + current_position_flash, data_len, data_incoming);
              // Perform Lock
              efl_lld_stop(&EFLD1);

              // For Debug Purposes
              #if DEBUG
                uint8_t *data_outgoing = (uint8_t *)malloc(sizeof(uint8_t)*(data_len+1));
                efl_lld_read(&EFLD1, 0x400*start_page, data_len, data_outgoing);

                sdWrite(&SD1, data_outgoing, data_len);
                free(data_outgoing);
              #endif

              //  Update the current flash position for storing next data
              current_position_flash += data_len;

              // Send Acknowledge Byte in order to let the PC know that data has been received
              sdPut(&SD1, ACK_BYTE);
            }
          }
          // free the dynamically allocated buffer
          free(data_incoming);
        }
      }

      // If 0xFF was received then start reading data from flash and writing it into serial port
      else if(received_char == 0xFF){
        
        // Single byte for storing data from flash
        uint8_t *data_outgoing = (uint8_t *)malloc(sizeof(uint8_t));
        // Read data from flash and write into serial port
        for(uint16_t byte_no=0; byte_no<current_position_flash; byte_no++){
          efl_lld_read(&EFLD1, 0x400*start_page+byte_no, 1, data_outgoing);
          sdPut(&SD1, *data_outgoing);
        }
        // free the dynamically allocated byte
        free(data_outgoing);
      }

    }
    
  }
}
