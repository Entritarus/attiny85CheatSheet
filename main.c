#include <avr/io.h> // register definitions
#include <avr/interrupt.h> // interrupts
#include <avr/sleep.h>

#define F_CPU 8000000L // cpu clock frequency
#include <util/delay.h> // delay functions for I2C
#define SCL_HIGH_TIME 5 // time in microseconds
#define SCL_LOW_TIME 5
#define HALF_TIME 2.5

#define SDA_PIN PB0
#define SCL_PIN PB2

#define SDA_INPUT_MODE DDRB &= ~(1<<SDA_PIN); // set SDA pin direction to input
#define SDA_OUTPUT_MODE DDRB |= 1<<SDA_PIN; // set SDA pin direction to output

#define PORTB_SDA_HIGH PORTB |= 1<<SDA_PIN; // set SDA pin level to high
#define PORTB_SDA_LOW PORTB &= ~(1<<SDA_PIN); // set SDA pin level to low
#define PORTB_SCL_HIGH PORTB |= 1<<SCL_PIN; // set SCL pin level to high
#define PORTB_SCL_LOW PORTB &= ~(1<<SCL_PIN); // set SCL pin level to low

#define WAIT_SDA_GO_LOW while((PORTB&(1<<SDA_PIN))); // loops while SDA pin is high, once it's low, it stops
#define WAIT_SDA_GO_HIGH while(!(PORTB&(1<<SDA_PIN))); // loops while SDA pin is low, once it's high, it stops
#define WAIT_SCL_GO_LOW while((PORTB&(1<<SCL_PIN))); // loops while SDA pin is high, once it's low, it stops
#define WAIT_SCL_GO_HIGH while(!(PORTB&(1<<SCL_PIN))); // loops while SDA pin is low, once it's high, it stops

#define USI_TIMER_MASK_8BIT 0b00001000 // timer masks used for counting bits
#define USI_TIMER_MASK_1BIT 0b00001111
#define USI_READ 0b00000001 // I2C read and write bits
#define USI_WRITE 0b00000000

// OLED display instructions
// instructions marked with p at the end require parameters (refer to the SSD1306 reference manual)
#define SET_CONTRAST_CONTROLP 0x81
#define ENTIRE_DISPLAY_ON_FOLLOW_RAM 0xA4
#define ENTIRE_DISPLAY_ON_IGNORE_RAM 0xA5
#define SET_NORMAL_DISPLAY 0xA6
#define SET_INVERSE_DISPLAY 0xA7
#define SET_DISPLAY_OFF 0xAE
#define SET_DISPLAY_ON 0xAF

#define CONTINUOUS_HORIZONTAL_RIGHT_SCROLL_SETUPP 0x26
#define CONTINUOUS_HORIZONTAL_LEFT_SCROLL_SETUPP 0x27
#define CONTINUOUS_VERTICAL_AND_HORIZONTAL_RIGHT_SCROLL_SETUPP 0x29
#define CONTINUOUS_VERTICAL_AND_HORIZONTAL_LEFT_SCROLL_SETUPP 0x2A
#define DEACTIVATE_SCROLL 0x2E
#define ACTIVATE_SCROLL 0x2F
#define SET_VERTICAL_SCROLL_AREAP 0xA3

#define SET_LOWER_COLUMN_START_ADDRESS 0x0
#define SET_HIGHER_COLUMN_START_ADDRESS 0x10
#define SET_MEMORY_ADDRESSING_MODEP 0x20
#define SET_COLUMN_ADDRESSP 0x21
#define SET_PAGE_ADDRESSP 0x22
#define SET_PAGE_START_ADDRESS 0xB0

#define SET_DISPLAY_START_LINE 0x40
#define SET_SEGMENT_REMAP 0xA0
#define SET_MUX_RATIOP 0xA8
#define SET_COM_OUTPUT_SCAN_DIRECTION 0xC0
#define SET_DISPLAY_OFFSETP 0xD3
#define SET_COM_PINS_HARDWARE_CONFIGP 0xDA

#define SET_DISPLAY_CLOCK_DIVIDE_RATIO_OSCILLATOR_FREQUENCYP 0xD5
#define SET_PRECHARGE_PERIODP 0xD9
#define SET_VCOMH_DESELECT_LEVELP 0xDB
#define NO_OPERATION 0xE3

// bytes used by OLED to distinguish data from instuctions
#define SINGLE_COMMAND_BYTE 0b10000000
#define COMMAND_STREAM 0b00000000
#define SINGLE_DATA_BYTE 0b11000000
#define DATA_STREAM 0b01000000

// I2C device addresses
#define OLED_SCREEN 0b01111000
#define EEPROM 0b10100000

// macros for letter displaying
#define MAX_LETTERS_PER_LINE 21
#define MAX_LINES 8
#define MAX_SUBPAGES 16
#define SUBPAGES_PER_MEMPAGE 2

// symbolic definitions
#include "flipped_letters.h"

// variables used for controlling the text output to the OLED display
uint8_t _g_linepos = 0;
uint8_t _g_linenum = 0;
int8_t _g_currentsubPage = 0;

// send an I2C start condition
void StartCondition() {
	USIDR = 0;
	PORTB_SDA_LOW
	WAIT_SDA_GO_LOW
	_delay_us(SCL_HIGH_TIME);
	PORTB_SCL_LOW
	WAIT_SCL_GO_LOW
	PORTB_SDA_HIGH
	_delay_us(HALF_TIME);
}
// send an I2C stop condition
void I2CDisconnect() { 
	_delay_us(HALF_TIME);
	//_delay_ms(200);
	PORTB_SCL_HIGH
	WAIT_SCL_GO_HIGH
	_delay_us(SCL_HIGH_TIME);
	//_delay_ms(200);
	USIDR = ~0;
	PORTB_SDA_HIGH;
	WAIT_SDA_GO_HIGH
	_delay_us(SCL_HIGH_TIME);
}

// send bits to a device. Must call acknowledge after this
uint8_t USISend(int8_t temp_USISR, int8_t data) {
	USIDR = data;
	USISR |= temp_USISR; 
	while ((USISR & 0b00001111) > 0) { 
		_delay_us(HALF_TIME);
		//_delay_ms(200);
		PORTB_SCL_HIGH
		WAIT_SCL_GO_HIGH
		_delay_us(SCL_HIGH_TIME);
		//_delay_ms(200);
		PORTB_SCL_LOW
		WAIT_SCL_GO_LOW
		_delay_us(HALF_TIME);
		
		USICR |= 1<<USICLK;
		USICR &= ~(1<<USICLK);
	}
	return USIDR;
}

// receive bits from device
uint8_t USIReceive(uint8_t temp_USISR) {
	USIDR = 0;
	USISR |= temp_USISR;
	SDA_INPUT_MODE
	while ((USISR & 0b00001111) > 0) {
		_delay_us(HALF_TIME);
		//_delay_ms(200);
		PORTB_SCL_HIGH
		WAIT_SCL_GO_HIGH
		USICR |= 1<<USICLK;
		USICR &= ~(1<<USICLK);
		_delay_us(SCL_HIGH_TIME);
		//_delay_ms(200);
		PORTB_SCL_LOW
		WAIT_SCL_GO_LOW
		_delay_us(HALF_TIME);
	}
	SDA_OUTPUT_MODE
	return USIDR;
}
// I2C acknowledge
// USISend can be used directly after calling this function.
uint8_t Acknowledge() {
	USIDR = 0;
	SDA_INPUT_MODE
	USISR |= USI_TIMER_MASK_1BIT;
	while ((USISR & 0b00001111) > 0) {
		_delay_us(HALF_TIME);
		//_delay_ms(200);
		PORTB_SCL_HIGH
		WAIT_SCL_GO_HIGH
		USICR |= 1<<USICLK;
		USICR &= ~(1<<USICLK);
		_delay_us(SCL_HIGH_TIME);
		//_delay_ms(200);
		PORTB_SCL_LOW
		WAIT_SCL_GO_LOW
		_delay_us(HALF_TIME);
		
	}
	SDA_OUTPUT_MODE
	return USIDR;
}

// set up USI registers to I2C mode
void initI2C() {
	DDRB |= (1<<SCL_PIN)|(1<<SDA_PIN);
	PORTB = (1<<SCL_PIN)|(1<<SDA_PIN);
	USIDR = 0b11101010;
	USICR = (1<<USIWM1);
}

//connect to a device via an address.
int8_t I2CMasterConnect(int8_t address) { 
	StartCondition();
	USISend(USI_TIMER_MASK_8BIT, address);
	if ((Acknowledge()&1)) return 0; 
	return 1;
}


//
// It's all about OLED from here
//

// send a control byte for ssd1306
int8_t SSD1306SendControlByte(int8_t controlByte) {
	USISend(USI_TIMER_MASK_8BIT, controlByte);
	if(Acknowledge()&1) return 0;
	return 1;
}
// sends a command sequence described in ssd1306 datasheet. seems to make ssd1306 operate without any offsets and scrolling
int8_t initSSD1306() { 
	
	if(!I2CMasterConnect(OLED_SCREEN)) return 0;
	if(!SSD1306SendControlByte(COMMAND_STREAM)) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_DISPLAY_OFF);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_MUX_RATIOP); //set MUX ratio
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0x3F);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_DISPLAY_OFFSETP);
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0x00);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_DISPLAY_START_LINE);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_SEGMENT_REMAP|1);//|1 is right
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_COM_OUTPUT_SCAN_DIRECTION|8);//|8 is right
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_COM_PINS_HARDWARE_CONFIGP);
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0322);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_CONTRAST_CONTROLP);
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0x7f);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, ENTIRE_DISPLAY_ON_FOLLOW_RAM);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_NORMAL_DISPLAY);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_DISPLAY_CLOCK_DIVIDE_RATIO_OSCILLATOR_FREQUENCYP);
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0x80);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, 0x8D); // enable charge pump regulator
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0x14);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_VCOMH_DESELECT_LEVELP); // vcomh deselect level to reset val
	if (Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 1<<5);
	if (Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_MEMORY_ADDRESSING_MODEP); // horizontal addressing mode
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0);
	if(Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_PRECHARGE_PERIODP); // set precharge period to reset values 
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, (1<<5)|(1<<1));
	if(Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_DISPLAY_ON);
	if (Acknowledge()&1) return 0;
	I2CDisconnect();
	//_delay_ms(1);
	return 1;
}

 //set up display to print next symbol
int8_t translatePos() {
	if(!I2CMasterConnect(OLED_SCREEN)) return 0;
	if(!SSD1306SendControlByte(COMMAND_STREAM)) return 0;
	if(_g_linepos == 21) {
		_g_linepos = 0;
		_g_linenum++;
		if(_g_linenum == 8) _g_linenum = 0;
	}
	USISend(USI_TIMER_MASK_8BIT, SET_PAGE_ADDRESSP);// set page start/end addresses to next line
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, _g_linenum);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 7);
	if(Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_COLUMN_ADDRESSP);// put cursor to the beginning of the new line
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, _g_linepos*6);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 127);
	if(Acknowledge()&1) return 0;
	I2CDisconnect();
	//_delay_ms(1);
	return 1;
}
 //clears the screen and sets column pointer to 0 at 0th page
int8_t clearScreen() {
	if(!I2CMasterConnect(OLED_SCREEN)) return 0;
	if(!SSD1306SendControlByte(COMMAND_STREAM)) return 0;
	USISend(USI_TIMER_MASK_8BIT, SET_PAGE_ADDRESSP);// set page start/end addresses to next line
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 7);
	if(Acknowledge()&1) return 0;
	
	USISend(USI_TIMER_MASK_8BIT, SET_COLUMN_ADDRESSP);// put cursor to the beginning of the new line
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 0);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, 127);
	if(Acknowledge()&1) return 0;
	I2CDisconnect();
	//_delay_ms(1);
	if(!I2CMasterConnect(OLED_SCREEN)) return 0;//send zeros
	if(!SSD1306SendControlByte(DATA_STREAM)) return 0;
	for (uint16_t i = 0; i < 1024; i++) {
		USISend(USI_TIMER_MASK_8BIT,0);
		if(Acknowledge()&1) return 0;
	}
	I2CDisconnect();
	//_delay_ms(1);
	
	_g_linenum = 0;
	_g_linepos = 0;
	return 1;
}
 //send symbol data
int8_t printSegment(uint8_t seg1, uint8_t seg2, uint8_t seg3, uint8_t seg4, uint8_t seg5, uint8_t seg6) {
	translatePos();

	if(!I2CMasterConnect(OLED_SCREEN)) return 0;
	if(!SSD1306SendControlByte(DATA_STREAM)) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg1);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg2);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg3);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg4);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg5);
	if(Acknowledge()&1) return 0;
	USISend(USI_TIMER_MASK_8BIT, seg6);
	if(Acknowledge()&1) return 0;
	_g_linepos++;
	I2CDisconnect();
	//_delay_ms(1);
	return 1;
}

int8_t printChar(uint8_t letter) { //transforms ASCII symbols into image and prints it
	switch(letter) {
		case 'A':
		if (!printSegment(UPPER_A)) return 0;
		break;
		case 'B':
		if (!printSegment(UPPER_B)) return 0;
		break;
		case 'C':
		if (!printSegment(UPPER_C)) return 0;
		break;
		case 'D':
		if (!printSegment(UPPER_D)) return 0;
		break;
		case 'E':
		if (!printSegment(UPPER_E)) return 0;
		break;
		case 'F':
		if (!printSegment(UPPER_F)) return 0;
		break;
		case 'G':
		if (!printSegment(UPPER_G)) return 0;
		break;
		case 'H':
		if (!printSegment(UPPER_H)) return 0;
		break;
		case 'I':
		if (!printSegment(UPPER_I)) return 0;
		break;
		case 'J':
		if (!printSegment(UPPER_J)) return 0;
		break;
		case 'K':
		if (!printSegment(UPPER_K)) return 0;
		break;
		case 'L':
		if (!printSegment(UPPER_L)) return 0;
		break;
		case 'M':
		if (!printSegment(UPPER_M)) return 0;
		break;
		case 'N':
		if (!printSegment(UPPER_N)) return 0;
		break;
		case 'O':
		if (!printSegment(UPPER_O)) return 0;
		break;
		case 'P':
		if (!printSegment(UPPER_P)) return 0;
		break;
		case 'Q':
		if (!printSegment(UPPER_Q)) return 0;
		break;
		case 'R':
		if (!printSegment(UPPER_R)) return 0;
		break;
		case 'S':
		if (!printSegment(UPPER_S)) return 0;
		break;
		case 'T':
		if (!printSegment(UPPER_T)) return 0;
		break;
		case 'U':
		if (!printSegment(UPPER_U)) return 0;
		break;
		case 'V':
		if (!printSegment(UPPER_V)) return 0;
		break;
		case 'W':
		if (!printSegment(UPPER_W)) return 0;
		break;
		case 'X':
		if (!printSegment(UPPER_X)) return 0;
		break;
		case 'Y':
		if (!printSegment(UPPER_Y)) return 0;
		break;
		case 'Z':
		if (!printSegment(UPPER_Z)) return 0;
		break;

		case 'a':
		if (!printSegment(LOWER_A)) return 0;
		break;
		case 'b':
		if (!printSegment(LOWER_B)) return 0;
		break;
		case 'c':
		if (!printSegment(LOWER_C)) return 0;
		break;
		case 'd':
		if (!printSegment(LOWER_D)) return 0;
		break;
		case 'e':
		if (!printSegment(LOWER_E)) return 0;
		break;
		case 'f':
		if (!printSegment(LOWER_F)) return 0;
		break;
		case 'g':
		if (!printSegment(LOWER_G)) return 0;
		break;
		case 'h':
		if (!printSegment(LOWER_H)) return 0;
		break;
		case 'i':
		if (!printSegment(LOWER_I)) return 0;
		break;
		case 'j':
		if (!printSegment(LOWER_J)) return 0;
		break;
		case 'k':
		if (!printSegment(LOWER_K)) return 0;
		break;
		case 'l':
		if (!printSegment(LOWER_L)) return 0;
		break;
		case 'm':
		if (!printSegment(LOWER_M)) return 0;
		break;
		case 'n':
		if (!printSegment(LOWER_N)) return 0;
		break;
		case 'o':
		if (!printSegment(LOWER_O)) return 0;
		break;
		case 'p':
		if (!printSegment(LOWER_P)) return 0;
		break;
		case 'q':
		if (!printSegment(LOWER_Q)) return 0;
		break;
		case 'r':
		if (!printSegment(LOWER_R)) return 0;
		break;
		case 's':
		if (!printSegment(LOWER_S)) return 0;
		break;
		case 't':
		if (!printSegment(LOWER_T)) return 0;
		break;
		case 'u':
		if (!printSegment(LOWER_U)) return 0;
		break;
		case 'v':
		if (!printSegment(LOWER_V)) return 0;
		break;
		case 'w':
		if (!printSegment(LOWER_W)) return 0;
		break;
		case 'x':
		if (!printSegment(LOWER_X)) return 0;
		break;
		case 'y':
		if (!printSegment(LOWER_Y)) return 0;
		break;
		case 'z':
		if (!printSegment(LOWER_Z)) return 0;
		break;
		
		case ' ':
		if (!printSegment(SYM_SPACE)) return 0;
		break;
		case '!':
		if (!printSegment(SYM_EXMARK)) return 0;
		break;
		case '"':
		if (!printSegment(SYM_QUOTE)) return 0;
		break;
		case '#':
		if (!printSegment(SYM_HASH)) return 0;
		break;
		case '$':
		if (!printSegment(SYM_DOLLAR)) return 0;
		break;
		case '%':
		if (!printSegment(SYM_PERCENT)) return 0;
		break;
		case '&':
		if (!printSegment(SYM_AMPERSAND)) return 0;
		break;
		case '\'':
		if (!printSegment(SYM_SINGLEQUOTE)) return 0;
		break;
		case '(':
		if (!printSegment(SYM_LBRACKET)) return 0;
		break;
		case ')':
		if (!printSegment(SYM_RBRACKET)) return 0;
		break;
		case '*':
		if (!printSegment(SYM_ASTERISK)) return 0;
		break;
		case '+':
		if (!printSegment(SYM_PLUS)) return 0;
		break;
		case ',':
		if (!printSegment(SYM_COMMA)) return 0;
		break;
		case '-':
		if (!printSegment(SYM_MINUS)) return 0;
		break;
		case '.':
		if (!printSegment(SYM_PERIOD)) return 0;
		break;
		case '/':
		if (!printSegment(SYM_SLASH)) return 0;
		break;
		case '0':
		if (!printSegment(SYM_0)) return 0;
		break;
		case '1':
		if (!printSegment(SYM_1)) return 0;
		break;
		case '2':
		if (!printSegment(SYM_2)) return 0;
		break;
		case '3':
		if (!printSegment(SYM_3)) return 0;
		break;
		case '4':
		if (!printSegment(SYM_4)) return 0;
		break;
		case '5':
		if (!printSegment(SYM_5)) return 0;
		break;
		case '6':
		if (!printSegment(SYM_6)) return 0;
		break;
		case '7':
		if (!printSegment(SYM_7)) return 0;
		break;
		case '8':
		if (!printSegment(SYM_8)) return 0;
		break;
		case '9':
		if (!printSegment(SYM_9)) return 0;
		break;
		case ':':
		if (!printSegment(SYM_COLON)) return 0;
		break;
		case ';':
		if (!printSegment(SYM_SEMICOLON)) return 0;
		break;
		case '<':
		if (!printSegment(SYM_LESSTHAN)) return 0;
		break;
		case '=':
		if (!printSegment(SYM_EQUALS)) return 0;
		break;
		case '>':
		if (!printSegment(SYM_GREATERTHAN)) return 0;
		break;
		case '?':
		if (!printSegment(SYM_QMARK)) return 0;
		break;
		case '@':
		if (!printSegment(SYM_AT)) return 0;
		break;
		case '[':
		if (!printSegment(SYM_LSBRACKET)) return 0;
		break;
		case '\\':
		if (!printSegment(SYM_BACKSLASH)) return 0;
		break;
		case ']':
		if (!printSegment(SYM_RSBRACKET)) return 0;
		break;
		case '^':
		if (!printSegment(SYM_POWER)) return 0;
		break;
		case '_':
		if (!printSegment(SYM_UNDERSCORE)) return 0;
		break;
		case '`':
		if (!printSegment(SYM_ACCENT)) return 0;
		break;
		case '{':
		if (!printSegment(SYM_LFBRACKET)) return 0;
		break;
		case '|':
		if (!printSegment(SYM_VLINE)) return 0;
		break;
		case '}':
		if (!printSegment(SYM_RFBRACKET)) return 0;
		break;
		case '~':
		if (!printSegment(SYM_TILDE)) return 0;
		break;
	}
	return 1;
}

//
// main read and display function
//

uint8_t readDisplayPage(uint8_t subpage) {
	char character;
	if (subpage < MAX_SUBPAGES){
		//set address
		uint8_t readAddress = EEPROM|((subpage/SUBPAGES_PER_MEMPAGE)<<1)|1;
		uint8_t writeAddress = EEPROM|((subpage/SUBPAGES_PER_MEMPAGE)<<1);
		if(!I2CMasterConnect(writeAddress)) return 0; // connect to write address
		USISend(USI_TIMER_MASK_8BIT, (subpage<<7)); // send address
		if(Acknowledge()&1) return 0;
		I2CDisconnect(); // disconnect
		//if(!I2CMasterConnect(EEPROM|((subpage/SUBPAGES_PER_MEMPAGE)<<1)|1)) return 0;//connect to read
		for (uint8_t i = 0; i < 128; i++) { // request 128 bytes from memory
			if(!I2CMasterConnect(readAddress)) return 0;//connect to read
			character = USIReceive(USI_TIMER_MASK_8BIT);
			Acknowledge();
			I2CDisconnect();
			if(!printChar(character)) return 0;
		}
		//I2CDisconnect();
	}
	return 1;

}


// change pages on rising edge on pins 2 and 3
ISR(PCINT0_vect) {
	if (!(PINB&(1<<PB3))) { // next page, pin 2
		_g_currentsubPage++;
		if (_g_currentsubPage>=16) _g_currentsubPage = 0;
		clearScreen();
		readDisplayPage(_g_currentsubPage);
	}
	if (!(PINB&(1<<PB4))) { // previous page, pin 3
		_g_currentsubPage--;
		if (_g_currentsubPage<0) _g_currentsubPage = 15;
		clearScreen();
		readDisplayPage(_g_currentsubPage);
	}
	
}

int main(void)
{
	initI2C();
	GIMSK |= 1<<PCIE; //enable pin change interrupts in GISMK
	PCMSK |= (1<<PCINT3)|(1<<PCINT4); //enable interrupts for pins 2 and 3 in PCMSK
	PORTB |= (1<<PB3)|(1<<PB4); //set up pullups for pins 2 and 3
	MCUCR |= 1<<SM1; // choose a sleep mode (power-down mode)
	DDRB |= 2; // led output for signalizing an error
	initSSD1306();
	clearScreen();
	if(!readDisplayPage(0)) PORTB |= 2;
	sei(); // enable interrupts
    while (1)
    {
		sleep_mode();
    }
	return 0;
}