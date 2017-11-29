/* 
 * File:   Serial_Trial2/main.c
 * Author: Daniel
 *
 * Created on October 16, 2017, 3:26 PM
 */

#pragma config WDTEN=OFF
#pragma config FOSC=INTIO67
#pragma config XINST=OFF

#include "Lcd.h"
#include "funcdec.h"
#include <p18f46k22.h>
#include <stdio.h>
#include <stdlib.h>
#include "ADAS1000.h"
#include "Communication.h"
#include "General.h"
#include "RN.h"
#include "Serial.h"
#include "ADASfxns.h"
#include "mainbrain.h"

extern GetRegisterValue();

unsigned char data_buffer[48];
unsigned char logicAnalyzerOutputEnable = 0;            //true if outputting
unsigned char RadioCnt = 0;
unsigned char *FillingBuffPnt;   
unsigned long ADASdata;
char FillingBuff;               // currently filling buffer (0,1))
char radioBuff;                 // currently the radio buffer
unsigned char Buffer0[64], Buffer1[64];
unsigned char* radiodataptr;

extern UINT8_T outBuf[MAX_OUT_BUF_SZ],      	///< Serial transmit buffer
      	txHead,                       	///< Read & write indexes for the output buffer
      	txTail,						
      	inBuf[MAX_IN_BUF_SZ],         	///< Serial receive buffer
      	rxHead,                       	///< Read & Write Indexes for the input buffer
      	rxTail;

void High_Priority_ISR(void);

#pragma code InterruptVectorHigh = 0x08
void InterruptVectorHigh (void)
{
  _asm
    goto High_Priority_ISR 			// Jump to ISR Interrupt funciton.
  _endasm				// ISR name is unimportant but must match.
}

#pragma interrupt High_Priority_ISR  // "Interrupt" pragma for high priority
void High_Priority_ISR(void)		   // Interrupt fuction. Name is unimportant. 
{
    Serial_ISR();
}

void main() {
    /* Initialize variables used in the function */
    unsigned char init, i;
    unsigned char command;
    char head_tail_diff;
    int nBuffersOfData, nTotalBuffersOfData;    
    
    /* Set the PIC clock frequency */
    OSCCON = 0b01110110; // set clock to 16 MHz //moved from below RNinit() to here)
    
    /* Initialize the LCD */
    ANSELD = 0x00; // turn on digital input buffer
    TRISD  = 0x00; // LCD digital pins to output
    
    //Initialize Serial port
    TRISCbits.RC6=1;  //switched from 1 to be an output
    TRISCbits.RC7=1;
    ANSELC=0x00;
    
    //initialize interrupts
    RCONbits.IPEN	= 1; //Priority levels
    INTCONbits.GIEH = 1; //Enable interrupts
    IPR1bits.RC1IP = 1; //High priority
    IPR1bits.TX1IP = 1;
    
    InitBreak();
    SERInit();
    RNInit();
    
    /* Initialize the SPI interface with the ADAS */
    init = 0;
    init = ADAS1000_Init(ADAS1000_2KHZ_FRAME_RATE);    
    
    // initialize ports for logic analyzer output
    InitLogicAnalyzerOut();
    
//   ADAS_DATA_INIT();                //configure and start the ADAS data flow
    ADAS_TEST_TONE();

    while(command<=8){
        switch(command){
                command = recCommand(); //a program that receives from the rn, interprets it, and returns an int to be used in the switch case
            case 1:
                wakeADAS();  //a subroutine to wakeup the ADAS
                // command = 2;
                break;
            case 2:
                setPacingParam();  //a subroutine to set the pacing parameters
                // command = 3;
                break;
            case 3:
                setADASregister();  //subroutine that passes data collection parameters right into the ADAS
                // command = 4;
                break;
            case 4:
                setDt(); //set for how long you would want to collect data
                // command = 5;
                break;
            case 5:
                startPacing(); //if all parameters have been set, start pacing
                // command = 6;
                break;
            case 6:
                AcquireECGData(25); //if all parameters have been set, record data for the amount of time specified by setDt, and continuously be sending it out to the photon
                break;
            case 7:
                stopPacing(); //stop pacing the heart
                break;
            case 8:
                resetParams(); //unnecessary fxn, but could maybe see use - reset all parameter values that were set
                break;
            default:
                break;
        }
    };
}

void outputToLogicAnalyzer() {
    unsigned long newdata;
    newdata = ((unsigned long)data_buffer[5] << 16) +
                          ((unsigned long)data_buffer[6] << 8) +
                          ((unsigned long)data_buffer[7] << 0);

    newdata = newdata >> 16; // & 0b11111ul; // shift to get highest order bits in low byte

    if (newdata & 0x01) LATEbits.LATE0 = 1;
    else LATEbits.LATE0 = 0;
    if (newdata & 0x02) LATEbits.LATE1 = 1;
    else LATEbits.LATE1 = 0;
    if (newdata & 0x04) LATEbits.LATE2 = 1;
    else LATEbits.LATE2 = 0;
    if (newdata & 0x08) LATAbits.LATA4 = 1;
    else LATAbits.LATA4 = 0;
    if (newdata & 0x10) LATBbits.LATB4 = 1;
    else LATBbits.LATB4 = 0;
    if (newdata & 0x20) LATBbits.LATB5 = 1;
    else LATBbits.LATB5 = 0;
    if (newdata & 0x40) LATBbits.LATB6 = 1;
    else LATBbits.LATB6 = 0;
    if (newdata & 0x80) LATBbits.LATB7 = 1;
    else LATBbits.LATB7 = 0;

    LATAbits.LATA5 = 1;         //clock
    LATAbits.LATA5 = 0;
}                               // end if logicAnalyzer

// finite state machine for acquiring data from the ADAS and sending it out over the 
// RS232 the transmit buffer.
void AcquireECGData (unsigned int nTotalBuffersOfData)
{
    unsigned char acquireState = 0;
    int nBuffersOfData = 0;
    unsigned char radioCnt, FillingCounter;
    unsigned char readCmd[4] = {0, 0, 0, 0};
    
    while (acquireState <= 12){
        switch (acquireState){
            case 0:
                // Init buffer 0
                FillingBuffPnt = Buffer0;       // set the pointer
                FillingCounter = 0;                 // Init Counter
                acquireState = 1;
                readCmd[0] = ADAS1000_FRAMES;	// Register address.
                SPI_Write(readCmd, 4);
                break;
            case 1:                              // Fill buf 0, no radio xmit
                while (ADAS_DATA_NOT_READY);                                // wait for data from ADAS
                readFormatStoreSample();
                if (++FillingCounter >= 16) {
                    acquireState = 2; 
                    if (++nBuffersOfData >= nTotalBuffersOfData) {
                        ADAS1000_GetRegisterValue(ADAS1000_FRMCTL, readCmd);    // stop ADAS
                        acquireState = 8;         // case of only one buffer    
                    }
                }
                FillingBuffPnt++;
                break;
            case 2:                                                     // Init Fill buf 1, empty buf 0;
                FillingBuffPnt = Buffer1;       // set the pointer
                FillingCounter = 0;                 // Init Counter
                radiodataptr = Buffer0;
                radioCnt = 0;
                acquireState = 3;
                SERSendStr("radio tx ");
                break;
            case 3:                                                     // fill buf 1, empty 0                       
                if (ADAS_DATA_NOT_READY){           // When no ADAS data available 
                    if (moveRadioSample()) {                //  4 spots available? store, else not
                        if (++radioCnt >= 16) {             //radio buff sample counter
                            SERSendStr("\r\n"); //append char return line feed
                            acquireState = 4;       
                            break;
                        }
                    }
                    else break;                             // not enough room yet, go around again
                }
                else {                                            // if there is ADAS data ready
                    readFormatStoreSample();                    
                    ++FillingCounter;
                    // don't check filling counter here because filling buffer should not be full before radio is done
                }
                break;

            case 4:                                              //finish Fill buf 1, no radio xmit
                while (ADAS_DATA_NOT_READY);                                // wait for data from ADAS
                readFormatStoreSample();
                if (++FillingCounter >= 16) {                           // filling buffer full?
                    acquireState = 5;                                   // swap
                    if (++nBuffersOfData >= nTotalBuffersOfData) {
                        ADAS1000_GetRegisterValue(ADAS1000_FRMCTL, readCmd);    // stop ADAS
                        acquireState = 10;         // go to ending state
                    }   
                    break;
                }
                break;
            case 5:
                FillingBuffPnt = Buffer0;       // set the pointer
                FillingCounter = 0;                 // Init Counter
                radiodataptr = Buffer1;
                radioCnt = 0;
                acquireState = 6;
                SERSendStr("radio tx ");
                break;
            case 6:                                                     // fill buf 0, empty 1                       
                if (ADAS_DATA_NOT_READY){           // When no ADAS data available 
                    if (moveRadioSample()) {                //  4 spots available? store, else not
                        if (++radioCnt >= 16) {             //radio buff sample counter 
                            SERSendStr("\r\n"); //append char return line feed
                            acquireState = 7;       
                            break;
                        }
                    }
                    else break;                             // not enough room yet, go around again
                }
                else {                                            // if there is ADAS data ready
                    readFormatStoreSample();                    
                    ++FillingCounter;
                    // don't check filling counter here because filling buffer should not be full before radio is done
                }
                break;

            case 7:                                              //finish Fill buf 0, no radio xmit
                while (ADAS_DATA_NOT_READY);                                // wait for data from ADAS
                readFormatStoreSample();
                if (++FillingCounter >= 16) {                           // filling buffer full?
                    acquireState = 2;                                   // swap
                    if (++nBuffersOfData >= nTotalBuffersOfData) {
                        ADAS1000_GetRegisterValue(ADAS1000_FRMCTL, readCmd);    // stop ADAS
                        acquireState = 8;         // go to ending state
                    }   
                    break;
                }
                break;
            case 8:                                         // transmit only from 0, no data colloect
                radiodataptr = Buffer0;
                radioCnt = 0;
                SERSendStr("radio tx ");
                acquireState = 9;
            case 9:
                if (moveRadioSample()) {                //  4 spots available? store else not
                    if (++radioCnt >= 16) {             //radio buff sample counter 
                        acquireState = 12;       
                        break;
                    }
                }
                break;
            case 10:                                         // transmit only from 1, no data colloect
                radiodataptr = Buffer1;
                radioCnt = 0;
                SERSendStr("radio tx ");
                acquireState = 11;
            case 11:
                if (moveRadioSample()) {                //  4 spots available? store else not
                    if (++radioCnt >= 16) {             //radio buff sample counter 
                        acquireState = 12;
                        break;
                    }
                }
                break;
            case 12:
                SERSendStr("\r\n");                     //append char return line feed
                return;
        }
    }
}

 void readFormatStoreSample()
 {
    SPI_Read(data_buffer, 8);                                   // read the next data packet
    *FillingBuffPnt = ((data_buffer[5] >> 4) & 0x0F) + '0'; // store ASCII of high nibble high byte
    if(*FillingBuffPnt > '9')*FillingBuffPnt += 7;          //converts to true HEX
    FillingBuffPnt++;

    *FillingBuffPnt = (data_buffer[5] & 0x0F) + '0';    // store ASCII of low nibble high byte
    if(*FillingBuffPnt > '9')*FillingBuffPnt += 7;          //converts to true HEX
    FillingBuffPnt++;

    *FillingBuffPnt = ((data_buffer[6] >> 4) & 0x0F) +'0';    // store ASCII of high nibble low byte
    if(*FillingBuffPnt > '9')*FillingBuffPnt += 7;          //converts to true HEX
    FillingBuffPnt++;

    *FillingBuffPnt = (data_buffer[6] & 0x0F) + '0';    // store ASCII of low nibble low byte
    if(*FillingBuffPnt > '9')*FillingBuffPnt += 7;          //converts to true HEX
    FillingBuffPnt++;

    if(logicAnalyzerOutputEnable)outputToLogicAnalyzer();
 }

 unsigned char moveRadioSample ()
 {   
    char head_tail_diff;
    head_tail_diff = txHead - txTail;
    if (head_tail_diff < 0) head_tail_diff += MAX_OUT_BUF_SZ;
    if (MAX_OUT_BUF_SZ-head_tail_diff < 4)return (0);       // is there room for another 4 byte sample
    else {
        // get next 4 bytes from radio buffer and send to Tx Buffer
        SERTxSave(*radiodataptr++);
        SERTxSave(*radiodataptr++);
        SERTxSave(*radiodataptr++);
        SERTxSave(*radiodataptr++);
    }
    return(1);
 }