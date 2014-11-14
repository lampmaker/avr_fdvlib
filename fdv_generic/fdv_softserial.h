/*
SoftwareSerial.h (formerly NewSoftSerial.h) - 
Multi-instance software serial library for Arduino/Wiring
-- Interrupt-driven receive and other improvements by ladyada
   (http://ladyada.net)
-- Tuning, circular buffer, derivation from class Print/Stream,
   multi-instance support, porting to 8MHz processors,
   various optimizations, PROGMEM delay tables, inverse logic and 
   direct port writing by Mikal Hart (http://www.arduiniana.org)
-- Pin change interrupt macros by Paul Stoffregen (http://www.pjrc.com)
-- 20MHz processor support by Garrett Mace (http://www.macetech.com)
-- ATmega1280/2560 support by Brett Hagman (http://www.roguerobotics.com/)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

The latest version of this library can always be found at
http://arduiniana.org.
*/

/*
Adapted to fdvlib by Fabrizio Di Vittorio, fdivitto@gmail.com
*/

#ifndef FDV_SOFTSERIAL_H_
#define FDV_SOFTSERIAL_H_


#include <inttypes.h>

#include "fdv_pin.h"
#include "fdv_interrupt.h"


namespace fdv
{



#define _SS_MAX_RX_BUFF 64 // RX buffer size


//
// Lookup table
//
typedef struct _DELAY_TABLE
{
	long baud;
	unsigned short rx_delay_centering;
	unsigned short rx_delay_intrabit;
	unsigned short rx_delay_stopbit;
	unsigned short tx_delay;
} DELAY_TABLE;


#if F_CPU == 16000000

static const DELAY_TABLE PROGMEM table[] =
{
	//  baud    rxcenter   rxintra    rxstop    tx
	{ 115200,   1,         17,        17,       12,    },
	{ 57600,    10,        37,        37,       33,    },
	{ 38400,    25,        57,        57,       54,    },
	{ 31250,    31,        70,        70,       68,    },
	{ 28800,    34,        77,        77,       74,    },
	{ 19200,    54,        117,       117,      114,   },
	{ 14400,    74,        156,       156,      153,   },
	{ 9600,     114,       236,       236,      233,   },
	{ 4800,     233,       474,       474,      471,   },
	{ 2400,     471,       950,       950,      947,   },
	{ 1200,     947,       1902,      1902,     1899,  },
	{ 600,      1902,      3804,      3804,     3800,  },
	{ 300,      3804,      7617,      7617,     7614,  },
};

const int XMIT_START_ADJUSTMENT = 5;

#elif F_CPU == 8000000

static const DELAY_TABLE table[] PROGMEM =
{
	//  baud    rxcenter    rxintra    rxstop  tx
	{ 115200,   1,          5,         5,      3,      },
	{ 57600,    1,          15,        15,     13,     },
	{ 38400,    2,          25,        26,     23,     },
	{ 31250,    7,          32,        33,     29,     },
	{ 28800,    11,         35,        35,     32,     },
	{ 19200,    20,         55,        55,     52,     },
	{ 14400,    30,         75,        75,     72,     },
	{ 9600,     50,         114,       114,    112,    },
	{ 4800,     110,        233,       233,    230,    },
	{ 2400,     229,        472,       472,    469,    },
	{ 1200,     467,        948,       948,    945,    },
	{ 600,      948,        1895,      1895,   1890,   },
	{ 300,      1895,       3805,      3805,   3802,   },
};

const int XMIT_START_ADJUSTMENT = 4;

#elif F_CPU == 20000000

// 20MHz support courtesy of the good people at macegr.com.
// Thanks, Garrett!

static const DELAY_TABLE PROGMEM table[] =
{
	//  baud    rxcenter    rxintra    rxstop  tx
	{ 115200,   3,          21,        21,     18,     },
	{ 57600,    20,         43,        43,     41,     },
	{ 38400,    37,         73,        73,     70,     },
	{ 31250,    45,         89,        89,     88,     },
	{ 28800,    46,         98,        98,     95,     },
	{ 19200,    71,         148,       148,    145,    },
	{ 14400,    96,         197,       197,    194,    },
	{ 9600,     146,        297,       297,    294,    },
	{ 4800,     296,        595,       595,    592,    },
	{ 2400,     592,        1189,      1189,   1186,   },
	{ 1200,     1187,       2379,      2379,   2376,   },
	{ 600,      2379,       4759,      4759,   4755,   },
	{ 300,      4759,       9523,      9523,   9520,   },
};

const int XMIT_START_ADJUSTMENT = 6;

#else

#error This version of SoftwareSerial supports only 20, 16 and 8MHz processors

#endif




inline void tunedDelay(uint16_t delay)
{
	uint8_t tmp=0;

	asm volatile("sbiw    %0, 0x01 \n\t"
	"ldi %1, 0xFF \n\t"
	"cpi %A0, 0xFF \n\t"
	"cpc %B0, %1 \n\t"
	"brne .-10 \n\t"
	: "+r" (delay), "+a" (tmp)
	: "0" (delay)
	);
}


// start timer 1 (no prescaler, no interrupt)
#define TIMER1_START \
	TIMSK1 = 0; \
	TCCR1A = 0; \
	TCNT1  = 0; \
	OCR1A  = 0xFFFF; \
	TIFR1 |= (1 << OCF1A); \
	TCCR1B = 1 << CS10;

#define TIMER1_SETCHECKPOINT_A(value) \
  OCR1A = value;
	
#define TIMER1_WAITCHECKPOINT_A \
	while ((TIFR1 & (1 << OCF1A)) == 0) \
	  ; \
	TIFR1 |= (1 << OCF1A);

#define TIMER1_WAIT(value) \
  TIMER1_SETCHECKPOINT_A(value); \
	TIMER1_WAITCHECKPOINT_A


// Only one instance at the time can be enabled to receive (to enable call listen(), to disable listen(false)). 
// output pins: everyone
// input pins:  everyone
class SoftwareSerial : public PCExtInterrupt::IExtInterruptCallable
{
private:

	char _receive_buffer[_SS_MAX_RX_BUFF];
	volatile uint8_t _receive_buffer_tail;
	volatile uint8_t _receive_buffer_head;

	Pin const* _receivePin;
	Pin const* _transmitPin;

	uint16_t _rx_delay_centering;
	uint16_t _rx_delay_intrabit;
	uint16_t _rx_delay_stopbit;
	uint16_t _tx_delay;

	bool _buffer_overflow;
	
	uint16_t _symbol_ticks;
	

	// private methods
	
	//
	// The receive routine called by the interrupt handler
	// Assume interrupts disabled
	void recv()
	{

		TIMER1_START;
		DEBUG_PIN.writeHigh();
		delayMicroseconds(5);
		DEBUG_PIN.writeLow();

		if (_receivePin->read() == 0)
		{
			uint16_t t = (_symbol_ticks >> 1) - 144;  // 9600=-144
			TIMER1_WAIT(t);
			DEBUG_PIN.writeHigh();
			delayMicroseconds(5);
			DEBUG_PIN.writeLow();

			uint8_t d = 0;
			for (uint8_t i = 0; i != 8; ++i)
			{
				t += _symbol_ticks;
				TIMER1_WAIT(t);
				d |= _receivePin->read() << i;
				DEBUG_PIN.writeHigh();
				delayMicroseconds(5);
				DEBUG_PIN.writeLow();
			}

			t += _symbol_ticks;
			TIMER1_WAIT(t);
			DEBUG_PIN.writeHigh();
			delayMicroseconds(5);
			DEBUG_PIN.writeLow();

			// if buffer full, set the overflow flag and return
			if ((_receive_buffer_tail + 1) % _SS_MAX_RX_BUFF != _receive_buffer_head)
			{
				// save new data in buffer: tail points to where byte goes
				_receive_buffer[_receive_buffer_tail] = d; // save new byte
				_receive_buffer_tail = (_receive_buffer_tail + 1) % _SS_MAX_RX_BUFF;
			}
			else
			{
				_buffer_overflow = true;
			}
		}

	}

	
	void setTX(Pin const* transmitPin)
	{
		_transmitPin = transmitPin;
		_transmitPin->modeOutput();
		_transmitPin->writeHigh();
	}

	
	void setRX(Pin const* receivePin)
	{
		_receivePin = receivePin;
		_receivePin->modeInput();
  	_receivePin->writeHigh(); // pullup
	}
	


public:
	
	// public methods
	SoftwareSerial(Pin const* receivePin, Pin const* transmitPin) :
    _receive_buffer_tail(0),
    _receive_buffer_head(0),
		_rx_delay_centering(0),
		_rx_delay_intrabit(0),
		_rx_delay_stopbit(0),
		_tx_delay(0),
		_buffer_overflow(false)
	{
		setTX(transmitPin);
		setRX(receivePin);
	}
	
	
	~SoftwareSerial()
	{
		end();
	}
	
	
	void begin(long speed)
	{
		_rx_delay_centering = _rx_delay_intrabit = _rx_delay_stopbit = _tx_delay = 0;

		for (unsigned i=0; i<sizeof(table)/sizeof(table[0]); ++i)
		{
			long baud = pgm_read_dword(&table[i].baud);
			if (baud == speed)
			{
				_rx_delay_centering = pgm_read_word(&table[i].rx_delay_centering);
				_rx_delay_intrabit = pgm_read_word(&table[i].rx_delay_intrabit);
				_rx_delay_stopbit = pgm_read_word(&table[i].rx_delay_stopbit);
				_tx_delay = pgm_read_word(&table[i].tx_delay);
				break;
			}
		}
		
		_symbol_ticks = F_CPU / speed;

		tunedDelay(_tx_delay); // if we were low this establishes the end

		listen();
	}
	
	
	// This function sets the current object as the "listening"
	// one
	void listen(bool enable = true)
	{
		PCExtInterrupt::attach(_receivePin->PCEXT_INT, NULL);
		if (enable)
		{
			ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
			{
				_buffer_overflow = false;
				_receive_buffer_head = _receive_buffer_tail = 0;
			}			
			PCExtInterrupt::attach(_receivePin->PCEXT_INT, this);
		}
	}
	
	
	void end()
	{
		listen(false);
	}
	
		
	int peek()
	{
		int r = -1;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			// Empty buffer?
			if (_receive_buffer_head == _receive_buffer_tail)
				r = -1;
			else
				// Read from "head"
				r = _receive_buffer[_receive_buffer_head];
		}
		return r;
	}
	

	size_t write(uint8_t b)
	{
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			// Write the start bit
			_transmitPin->writeLow();
			tunedDelay(_tx_delay + XMIT_START_ADJUSTMENT);

			// Write each of the 8 bits
			for (uint8_t mask = 0x01; mask; mask <<= 1)
			{
				if (b & mask) // choose bit
					_transmitPin->writeHigh(); // send 1
				else
					_transmitPin->writeLow(); // send 0
			
				tunedDelay(_tx_delay);
			}

			_transmitPin->writeHigh(); // restore pin to natural state

			tunedDelay(_tx_delay);
		}
		return 1;
	}
	
	
	int read()
	{
		int r = -1;

		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			// Empty buffer?
			if (_receive_buffer_head == _receive_buffer_tail)
				r = -1;
			else
			{
				// Read from "head"
				r = _receive_buffer[_receive_buffer_head]; // grab next byte
				_receive_buffer_head = (_receive_buffer_head + 1) % _SS_MAX_RX_BUFF;
			}
		}
		return r;
	}
	
	
	int available()
	{
		int r = 0;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			r = (_receive_buffer_tail + _SS_MAX_RX_BUFF - _receive_buffer_head) % _SS_MAX_RX_BUFF;
		}
		return r;
	}
	

	void flush()
	{
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			_receive_buffer_head = _receive_buffer_tail = 0;			
		}
	}
	
	
	// public only for easy access by interrupt handlers
	void extInterrupt()
	{
		recv();	
	}
};


}


#endif /* FDV_SOFTSERIAL_H_ */