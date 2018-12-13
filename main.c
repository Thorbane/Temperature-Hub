/*
 * jmcki007_122A_Final_Project.c
 *
 * Created: 11/10/2018 4:03:21 PM
 * Author : mckin
 */ 

#include <avr/io.h>
#include <stdio.h>
#include <stdlib.h>
#include "ds18b20.h"
#include "io.h"
#include "timer.h"
//define F_CPU before delay include
# define F_CPU 8000000UL
#include <util/delay.h>
#include "uart.h"
#include <string.h>


#define tasksNum 2
const unsigned long tasksPeriodGCD = 1000;
enum wifi_state{POWERDOWN, BOOT, WAIT, ON, READY};
enum temp_sensor_state{CONVERT,RETURN};

typedef struct task {
    int state; // Current state of the task
    unsigned long period; // Rate at which the task should tick
    unsigned long elapsedTime; // Time since task's previous tick
    int (*TickFct)(int); // Function to call for task's tick
} task;

task tasks[tasksNum];

void TimerISR() {
    unsigned char i;
    TimerFlag = 1;
    for (i = 0; i < tasksNum; ++i)
    {
        if ( tasks[i].elapsedTime >= tasks[i].period )
        {
            tasks[i].state = tasks[i].TickFct(tasks[i].state);
            tasks[i].elapsedTime = 0;
        }
        tasks[i].elapsedTime += tasksPeriodGCD;
    }
}

int temperature = 0x0000;
int u_temp = 0x0000;
char temp_string[12];
char temp_remote_string[16] = "BLAH";
volatile unsigned char received_flag = 0;
volatile unsigned char received = 0;
volatile unsigned char pd = 0;

char str[64];


int get_temperature(int state)
{
	switch(state)
	{
		case CONVERT:
			if (ds18b20convert( &PORTA, &DDRA, &PINA, 0x01, NULL ))
			{
				PORTB = 0x80;
			}
			else
			{
				PORTB = 0x01;
				state = RETURN;
			}
			break;
		
		case RETURN:
			ds18b20read( &PORTA, &DDRA, &PINA, 0x01, NULL, &temperature );
			state = CONVERT;
			break;
	}
    
    return state;
}

int get_temperature_USART()
{
	if (uart0_available() > 1)
	{
		volatile char MSB, LSB;
		received = uart0_available();
		MSB = (char) uart0_getc();
		LSB = (char) uart0_getc();
		
		u_temp = MSB;
		u_temp = u_temp << 8;
		u_temp = u_temp | LSB;
		
		uart0_flush();
		
		return 1;
	}
	
	return 0;
}

void LCD_write_S(char * str)
{
	LCD_ClearScreen();
	unsigned char c = 1;
	char *strpointer = str;
	while(*strpointer) {
		LCD_Cursor(c++);
		LCD_WriteData(*strpointer++);
	}
	LCD_Cursor(33);
}

void get_response(char * str, int len)
{
	if (uart0_available())
	{		
		unsigned int i;
		for (i=0; (uart0_available() && i<len-1); i++)
		{
			str[i] = uart0_getc();
		}
		str[i++] = 0;
	}
}

void display_response()
{
	char str[33] = "";
	char x;
	unsigned int j = 0;
	for (unsigned int i = 0; (x = (char) uart0_getc()) && i<32; i++)
	{
		if (x != '\n' && x!= '\r')
		{
			str[j] = x;
			j++;
		}
	}
	uart0_flush();
		
	LCD_write_S(str);
}

void display_response_raw(char * str)
{
	unsigned int i = 0;
	for (i = 0; (char) uart0_peek() && i<32; i++)
	{
		str[i] = uart0_getc();
	}
	str[i+1] = 0;
	uart0_flush();
	
	LCD_write_S(str);
}

void display_input_line()
{
	char str[33] = "";
	if (uart0_available())
	{
		while (uart0_peek() == '\n' || uart0_peek() == '\r')
		{
			uart0_getc();
		}
		while (uart0_available() && uart0_peek() != '\r')
		{
			char tempstr[2];
			tempstr[0] = uart0_getc();
			tempstr[1] = 0;
			strcat(str, tempstr);
		}
		
		LCD_ClearScreen();
		LCD_write_S(str);
	}
}

int task_wifi(int state)
{
	static char boot_timer = 3;
	static unsigned char timer = 0;
	
	switch (state) // transitions
	{
		case BOOT:
			if (!boot_timer) // if timer == 0
			{
				state = ON;
				uart0_puts("ATE0\r\n");
				_delay_ms(50);
				uart0_flush();
				_delay_ms(50);
				uart0_puts("AT+CIPMUX=1\r\n");
				_delay_ms(50);
				display_input_line();
				_delay_ms(1000);
				uart0_puts("AT+CWSAP=\"TemperatureHub\",\"abc123456\",5,0\r\n");
				_delay_ms(50);
				display_input_line();
				_delay_ms(1000);
				uart0_flush();
			}
			else
			{
				boot_timer--;
			}
			break;
		case ON:
			//LCD_write_S("State = ON");
			uart0_flush();
			char str[64] = "";
			uart0_puts("AT\r\n");
			_delay_ms(50);
			get_response(str,64);
			if(strstr(str, "OK"))
			{
				LCD_write_S("STARTING SERVER");
				uart0_puts("AT+CIPAP=\"192.168.64.64\"\r\n");
				_delay_ms(1000);
				uart0_puts("AT+CIPSERVER=1,80\r\n");
				LCD_write_S("AT+CIPSERVER=1,80");
				timer = 4;
				state = WAIT;
			}
			else
			{
				state = ON;
			}
			
			if (pd)
			{
				state = POWERDOWN;
				PORTD &= 0xF7;
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				break;
			}
			break;
			
		case WAIT:
			if(timer == 0)
			{
				state = READY;
			}
			else
			{
				timer--;
			}
			break;
			
			
		case POWERDOWN:
			if(!pd)
			{
				state = BOOT;
				PORTD |= 0x08;
			}
			break;
			
		case READY:
			//LCD_write_S("READY");
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			break;
			
		default:
			state = BOOT;
			PORTD |= 0x08;
			LCD_write_S("BLARG");
			_delay_ms(1000);
			break;
		
	}
	
	switch (state) // Actions
	{
		case ON:
			break;
			
		case BOOT:
			LCD_write_S("BOOT");
			break;
			
		case READY:
			while (uart0_available())
			{
				char * ptr = NULL;
				int index = 0;
				get_response(str,63);
				if((ptr = strstr(str,"Temp:")))
				{
					//LCD_write_S("GOT DATA");
					index = ptr - str;
					unsigned char i = 0;
					for (i=0; i<16 && str[index]; i++)
					{
						temp_remote_string[i] = str[index];
						index++;
					}
					temp_remote_string[i]=0;
					received_flag = 1;
				}
			}
			static unsigned char k = 0;
			if(k >= 17)
			{
				uart0_puts("AT+CWLIF\r\n");
				k = 0;
				uart0_flush();
				_delay_ms(100);
				display_input_line();
				_delay_ms(1000);				
			}
			else
			{
				k++;
				//LCD_write_S(temp_remote_string);
			}
			if (k==0)
			{
				//display_response();
			}
			k++;
			break;
			
		case POWERDOWN:
			break;
	}
	

	
	return state;
}

int display_temperature(int state)
{
	LCD_write_S("DISP TEMP");
	//_delay_ms(1000);
	int temp_deg = temperature/16;
	int temp_frac = (temperature & 0x000F) * 625;
	/*
	if(get_temperature_USART())
	{
		received_flag = 1;
		int temp_remote = u_temp;
		int temp_remote_deg = temp_remote/16;
		int temp_remote_frac = (temp_remote & 0x000F) * 625;
		sprintf(temp_string,"%i.%04i", temp_deg, temp_frac);
		sprintf(temp_remote_string,"%i.%04i   %i", temp_remote_deg, temp_remote_frac, uart0_available());
	}*/
	
	char str_temp[3];
	char str_frac[4];
	
	itoa(temp_deg, str_temp, 10);
	itoa(temp_frac, str_frac, 10);
	
	
	if (!received_flag)
	{
		strcpy(temp_remote_string, "NO DATA");
	}
	
	
	// Output to LCD
	//LCD_ClearScreen();
	LCD_write_S("Hello");
    unsigned char c = 1;
    char *strpointer = temp_string;
    /*while(*strpointer) {
        LCD_Cursor(c++);
        LCD_WriteData(*strpointer++);
    }*/
	
	c = 1;
	strpointer = temp_remote_string;
	while(*strpointer) {
		LCD_Cursor(c++);
		LCD_WriteData(*strpointer++);
	}
	
	LCD_Cursor(33);
    
    return state;
}

int main(void)
{	
    DDRB = 0xFF;
    PORTB = 0x00;
    
    DDRD = 0xFE;
    PORTD = 0x01;
    
    DDRC = 0xFF;
    PORTC = 0x00;
    
    unsigned char i = 0;
    /*tasks[i].state = -1;
    tasks[i].period = 1000;
    tasks[i].elapsedTime = 1000;
    tasks[i].TickFct = &get_temperature;
    i++;*/
    tasks[i].state = -1;
    tasks[i].period = 2000;
    tasks[i].elapsedTime = 0;
    tasks[i].TickFct = &display_temperature;
    i++;
	tasks[i].state = POWERDOWN;
	tasks[i].period = 1000;
	tasks[i].elapsedTime = 0;
	tasks[i].TickFct = &task_wifi;
	
	LCD_init();
	
	unsigned char c = 1;
	char temp_string[] = "LCD Init";
	char *strpointer = temp_string;
	while(*strpointer) {
		LCD_Cursor(c++);
		LCD_WriteData(*strpointer++);
	}
    
	//ds18b20wsp(&PORTA, &DDRA, &PINA, 0x01, NULL, 0, 127, 0x7F);
	
	uart0_init(UART_BAUD_SELECT_DOUBLE_SPEED(19200, F_CPU));
	
	
	TimerSet(tasksPeriodGCD);
	TimerOn();
	
    while (1) 
    {
    }
}

