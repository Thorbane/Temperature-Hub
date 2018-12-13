#ifndef __io_h__
#define __io_h__

#include <avr/interrupt.h>
#include <stdio.h>
#include <avr/io.h>

void LCD_init();
void LCD_ClearScreen(void);
void LCD_WriteCommand (unsigned char Command);
void LCD_WriteData(unsigned char Data);
void LCD_DisplayString(unsigned char column ,const unsigned char *string);
void LCD_Cursor (unsigned char column);
void delay_ms(int miliSec);
#endif
