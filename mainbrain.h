/* 
 * File:   mainbrain.h
 * Author: BME464
 *
 * Created on November 27, 2017, 5:11 PM
 */

#ifndef MAINBRAIN_H
#define	MAINBRAIN_H



unsigned char recCommand(void);
void wakeADAS(void);
void setPacingParam(void);
void setADASregister(void);
unsigned char setDt(void);
void startPacing(void);
void acquireData(unsigned int);
void stopPacing(void);
void resetParams(void);
char* parseSerial(void);  //unsure how to declare this.  I want a char array[32]



#endif

