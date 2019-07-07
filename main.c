/*
 * File:   main.c
 * Author: Branden
 *
 * Created on July 4, 2019, 5:20 PM
 */

#define DEBUG

#define F_CPU 20000000
#define F_CPU_MHZ F_CPU / 1000000
#define USART0_BAUD_RATE(BAUD_RATE) ((float)(F_CPU * 64 / (16 * (float)BAUD_RATE)) + 0)

#include <io.h>
#include <interrupt.h>

//#ifdef DEBUG

    #define CS_ENABLE() (PORTA.OUTCLR = PIN7_bm)
    #define CS_DISABLE() (PORTA.OUTSET = PIN7_bm)

//#else
//
//    #define CS_ENABLE() (PORTA.OUTCLR = PIN0_bm)
//    #define CS_DISABLE() (PORTA.OUTSET = PIN0_bm)
//
//#endif


#define TIMER_ENABLE() (TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm)
#define TIMER_DISABLE() (TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm)

#define MEM_WRITE_STATUS 0x01
#define MEM_WRITE 0x02
#define MEM_READ 0x03
#define MEM_READ_STATUS 0x05
#define MEM_WRITE_EN 0x06
#define MEM_FAST_READ 0x0B
#define MEM_SECT_ERASE 0x20
#define MEM_CHIP_ERASE 0xC7
#define MEM_128K_ERASE 0xD2
#define MEM_READ_ID 0x9F

#define MEM_STAT_WEN 0x02
#define MEM_STAT_BUSY 0x01

#define RX_WRITE_TEXT 0x10
#define RX_WRITE_DATA 0x20
#define RX_ERASE_ALL 0x30
#define RX_READ_ID 0x01
#define RX_READ_TEXT 0x11
#define RX_READ_DATA 0x21

#define MEM_BLOCK_SIZE 256
#define MEM_SECTOR_SIZE 4096

#define FILE_COUNT_ADDR MEM_SECTOR_SIZE + 4 //File count starts 4 bytes after second sector.

#define MIDI_MAX_MIDI_TRACKS 8

#define MIDI_NOTE_OFF_MASK 0x80 + 0x00
#define MIDI_NOTE_ON_MASK 0x80 + 0x10
#define MIDI_POLY_KEY_MASK 0x80 + 0x20
#define MIDI_CTRL_CHANGE_MASK 0x80 + 0x30
#define MIDI_PROG_CHANGE_MASK 0x80 + 0x40
#define MIDI_CH_PRESSURE_MASK 0x80 + 0x50
#define MIDI_PITCH_BEND_MASK 0x80 + 0x60
#define MIDI_SYS_EX 0xF0

#define MIDI_HEADER_TRACk_COUNT_OFFSET 10
#define MIDI_HEADER_TIME_DIV_OFFSET 12
#define MIDI_FIRST_TRACK_OFFSET 14

#define MIDI_START 0xFA
#define MIDI_CONTINUE 0xFB
#define MIDI_STOP 0xFC 
#define MIDI_UPDATE_MODE 0xFD

#define MIDI_INPUT_TIMER_PER 20000

#define INPUT_PAUSE_MASK 0x80
#define INPUT_PLAY_MODE_UPDATING_MASK 0x40
#define INPUT_PLAY_MODE_MASK 0x0C
#define INPUT_BUTTON_0_MASK 0x01
#define INPUT_BUTTON_1_MASK 0x02
#define PLAY_MODE_2_bm 0x08
#define PLAY_MODE_1_bm 0x04
#define PLAY_MODE_CLEAR_MASK 0xF3

#define SHORT_HOLD_INPUT_COUNT 5
#define LONG_HOLD_INPUT_COUNT 250
#define ADC_INPUT_0_WIN_LO 180
#define ADC_INPUT_0_WIN_HI 200



#define TIMER_PER_FOR_120BPM 9765 / 4 // div 4 because timer clock is also div4

typedef struct track_t
{
	uint32_t addressOffset;
	uint32_t startAddress;
	uint32_t deltaTime;
	uint8_t eventByte;
	uint8_t eventData1;
	uint8_t eventData2;	
} midiTrack;



volatile uint16_t _fileCount = 0;
volatile uint16_t _fileIndex = 0 ;
volatile uint32_t _fileAddressOffset = 0;
volatile uint16_t _tractCount = 0;
volatile uint8_t _activeTracks = 0;
volatile int16_t _division; //SIGNED
volatile uint32_t _tempo; //may be able to remove this;
volatile midiTrack _tracks[MIDI_MAX_MIDI_TRACKS];
volatile uint8_t _clockTickFlag = 0;
volatile uint8_t _input0Counts;
volatile uint8_t _input1Counts;
volatile uint8_t _statusFlags; //7 = Paused, 6 = Updating play mode, 5 = reserved, 4 = reserved, 3:2 = play mode, 1 = button 1 down, 0 = button 0 down

static inline void initClock(void)
{
    CCP = 0xD8;
    CLKCTRL.MCLKCTRLB = 0x00;
}

static inline void initTimers()
{
    //TCA0 Midi timer in normal mode
    TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV4_gc;// | TCA_SINGLE_ENABLE_bm;
    TCA0.SINGLE.PER = TIMER_PER_FOR_120BPM;
    
    //TCB0 Input timer in normal mode
    TCB0.CTRLB = TCB_CNTMODE_INT_gc;
    TCB0.INTCTRL = TCB_CAPT_bm;
    TCB0.CCMP = MIDI_INPUT_TIMER_PER;
    TCB0.CTRLA = TCB_ENABLE_bm | TCB_CLKSEL_CLKDIV2_gc;
	

}

static inline void initMIDIUART()
{
    PORTA.DIRSET = PIN6_bm;
    USART0.BAUD = (uint16_t)USART0_BAUD_RATE(31250);
    USART0.CTRLB = USART_TXEN_bm;
    USART0.CTRLC = USART_CHSIZE_8BIT_gc;
    
//#ifdef DEBUG
//    USART0.DEBUG = 1;
//#endif
    
}

static inline void initMemSPI()
{
    PORTA.DIRSET = PIN1_bm | PIN3_bm;
    
//#ifdef DEBUG
    PORTA.DIRSET = PIN7_bm;    
//#else
//    PORTA.DIRSET = PIN0_bm;
//#endif
    
    PORTA.DIRCLR = PIN2_bm;
    SPI0.CTRLA = SPI_MASTER_bm | SPI_CLK2X_bm | SPI_ENABLE_bm;// | SPI_DORD_bm;    
}

static inline void initInputADC()
{
//#ifndef DEBUG    
//    PORTA.OUTCLR = PIN7_bm;
//    PORTA.PIN7CTRL = 0x04;
//#endif
//
//    ADC0.CTRLC = ADC_PRESC_DIV256_gc | ADC_REFSEL_VDDREF_gc; 
//    ADC0.CTRLA = ADC_ENABLE_bm | ADC_RESSEL_8BIT_gc | ADC_ENABLE_bm;    
//    ADC0.MUXPOS = ADC_MUXPOS_AIN7_gc; 
    PORTA.OUTCLR = PIN0_bm;
    PORTA.PIN0CTRL = 0x04;
    ADC0.CTRLC = ADC_PRESC_DIV256_gc | ADC_REFSEL_VDDREF_gc; /* CLK_PER divided by 4 *//* Internal reference */
    ADC0.CTRLA = ADC_ENABLE_bm | ADC_RESSEL_8BIT_gc | ADC_FREERUN_bm; /* 10-bit mode */ /* ADC Enable: enabled */ 
    ADC0.MUXPOS = ADC_MUXPOS_AIN0_gc; /* Select ADC channel */    
}

static inline void initInterrupts()
{
    SREG = 1;
    sei();    
}

void sendMidi(uint8_t data)
{
	while(!(USART0.STATUS & USART_DREIF_bm)) {}
		
	USART0.TXDATAL = data;
}

uint8_t sendSPI(uint8_t data)
{
	SPI0.DATA = data;
	
	while(!(SPI0.INTFLAGS & SPI_IF_bm)) {}
	
	return SPI0.DATA;
	
}

static inline uint8_t sendDummy()
{
	return sendSPI(0x00);
}

void memSendAddress(uint32_t address)
{
    uint8_t *ptr = &address;
    sendSPI(*(ptr + 2));
    sendSPI(*(ptr + 1));
    sendSPI(*ptr);
}

uint8_t getMemStatus()
{
	uint8_t out;
	CS_DISABLE();
	CS_ENABLE();
	
	sendSPI(MEM_READ_STATUS);
	out = sendDummy();
	CS_DISABLE();
	return out;
}

void waitForNotBusy()
{
	while((getMemStatus() & MEM_STAT_BUSY)) {}
}

void memReadToVariable(uint32_t address, void *buff, uint8_t len)
{
	uint8_t *ptr = buff;
	waitForNotBusy();

	CS_DISABLE();
	CS_ENABLE();
	
	sendSPI(MEM_READ);
	memSendAddress(address);
	
    
    
	do
	{
        len--;
		*(ptr + len) = sendDummy();
	}while(len);
	
	CS_DISABLE();
	return;
	
}


//Sets _fileCount
void getFileCount()
{
    memReadToVariable(FILE_COUNT_ADDR, &_fileCount, 2);
}

//Uses _fileIndex
//Sets _fileAddressOffset
//Since we don't need the file names, we will automatically add the name length to the offset
void moveToFile()
{
	
	uint32_t newAddressOffset;
	uint16_t smallAddressOffset;
	uint32_t address = FILE_COUNT_ADDR + 2; //starting address for lookup table is 2 bytes after the size of the total data section.
	_fileAddressOffset = address + (uint32_t)(_fileCount * 4); //first file address is the address of the first lookup table entry + all entries (Which are each 4 bytes)
    
	//if the index we are looking for is 0 or the file count is 0, then we can leave the address we initialized _fileAddressOffset to
	if (_fileIndex > 0 && _fileCount > 0)
	{
		//Make sure the index is not larger than the file count
		if (_fileIndex >= _fileCount)
		{
			_fileIndex = _fileCount - 1;
		}	
		
		//We are basically summing up all of the file sizes until we reach our file index
		for(uint16_t i = 0; i < _fileIndex; i++)
		{
			//read file sizes and add them too the offsets			
			//memReadToBuffer(address, data, 4);
			memReadToVariable(address, &newAddressOffset, 4);
			_fileAddressOffset += newAddressOffset;
			address += 4;
		}
	}
	
	//As a convenience, here we add file name length to offset.
	memReadToVariable(_fileAddressOffset, &smallAddressOffset, 2);
	_fileAddressOffset +=  smallAddressOffset + 2; //+ 2 for the name length bytes
}

//Uses _fileAddressOffset
//Sets _trackCount
void getTrackCount()
{
	memReadToVariable(_fileAddressOffset + MIDI_HEADER_TRACk_COUNT_OFFSET, &_tractCount, 2);
}


//Uses _fileAddressOffset
//Sets _division
void getTimeDivision()
{
	//If _division is positive it represents Ticks per quarter note. By default the tempo is 120BPM, or 120 quarter notes per minute, 1 quarter note in 0.5 seconds
	//This would mean a tick (a clock tick) would be 0.5/_division. e.g. with a _division of 256, each tick would be 0.001953125 seconds long.
	//With this, an 8th note would have an even length (delta-time for the following Note-off event for that note) of 128 ticks, or a duration of 0.25 seconds.
	//A change in Tempo (meta-event 0xFF with an arg of 51) would effectively change the BPM.
	//For the SMPTE, the _division is calculated by multiplying the first byte by the second byte and then by -1. This means a value of 0xE7 0x28 would be the same as 0x03 0xE8 (or 1000uS, 1mS)	
	
	memReadToVariable(_fileAddressOffset + MIDI_HEADER_TIME_DIV_OFFSET, &_division, 2);
	
	if (_division < 0) //_division is negative so is in SMPTE format
	{
		_division = -1 * (_division >> 8) * (_division & 0xFF);
	}	
	
}



uint8_t getNextEvent(uint8_t trackIndex)
{
	uint8_t done = 0;
	uint32_t value = 0;
	uint8_t i;
    uint8_t eventByte;
	uint32_t addressOffset = _tracks[trackIndex].addressOffset;	
    uint8_t temp = 0;
	uint8_t *tempoPtr = &_tempo;        
    
	_tracks[trackIndex].deltaTime = 0;
	

	while(!done) 
	{

		//Get the variable length delta time
        i = 0;
        value = 0;
        while(i < 4)
        {

            value *= 128;

            temp = sendDummy();
            i++;       
            if ((temp & 0x80))
            {
                value += (temp & 0x7F);
            }
            else
            {

                value += temp;
                _tracks[trackIndex].deltaTime += value;
                break;
            }
            
        }
         
        addressOffset+=i;
		i = 0;
			
		eventByte = sendDummy(); // this should be the event byte
		

		
		addressOffset++;
			


		if ((eventByte & 0xF0) == MIDI_NOTE_ON_MASK || (eventByte & 0xF0) == MIDI_NOTE_OFF_MASK || (eventByte & 0xF0) == MIDI_PITCH_BEND_MASK)
		{
			//These are valid events that we want to handle, so capture the next two bytes and move to the next track
			_tracks[trackIndex].eventByte = eventByte;
			_tracks[trackIndex].eventData1 = sendDummy();
			_tracks[trackIndex].eventData2 = sendDummy();
			_tracks[trackIndex].addressOffset = addressOffset + 2;



			return 0xFF; 			
		}
		if ((eventByte & 0xF0) == MIDI_POLY_KEY_MASK || (eventByte & 0xF0) == MIDI_CTRL_CHANGE_MASK)//if the event is one of these, skip the next two bytes and continue.
		{
			sendDummy();
			sendDummy();
			addressOffset += 2;
		}
		if ( (eventByte & 0xF0) == MIDI_PROG_CHANGE_MASK || (eventByte & 0xF0) == MIDI_CH_PRESSURE_MASK) //if the event is one of these, skip the next byte and continue.
		{
			sendDummy();
			addressOffset++;
		}
		//Now we have to check other event types to know how far to skip ahead
		else if(eventByte == 0xFF) //Meta-Events
		{

			addressOffset++; //inc ahead of time for sendDummy
			//Here we are just looking for the number of bytes to jump, except for in the case of FF 51 03 (Tempo)
			switch(sendDummy())
			{
				case 0x00:
					i = 1;
					break;
				case 0x01: //all of the following are text that we can skip
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
				case 0x08:
				case 0x09:
					i = sendDummy(); //next byte is len of text
					addressOffset++;
					break;
				case 0x20:
					i = 2;
					break;
				case 0x2F: //End of track
					return 0x2F; 
					//_tracks[trackIndex].eventByte = 0x2F;
					//i = 1;
					//break;
				case 0x51:
					// skip the 0x03 control byte
					sendDummy();
					//read in the data bytes for the tempo
					_tempo = 0;
					*(tempoPtr + 2) = sendDummy();
					*(tempoPtr + 1) = sendDummy();
					*tempoPtr = sendDummy();
                    _tempo *= F_CPU_MHZ;
					TCA0.SINGLE.PER = (_tempo/(uint32_t)_division) / 4; //Divided by 4 so we can fit larger values. Timer clock also div4
		
					addressOffset += 4;
					i = 0;
					break;
				case 0x54:
					i = 6;
					break;
				case 0x58:
				i = 5;
				break;
				case 0x59:
				i = 3;
				break;
				case 0xF7:
           
                do
                {
                    addressOffset++;  
                }while(sendDummy() != 0xF7);
                
				i = 0;
				break;
			}
				
			addressOffset += i;
			//skip some bytes
			while(i--)
			{
				sendDummy();
			}
				
			
		}
			
	}
	
	return 0x00;
}

//Gets up to the max number of MIDI tracks. Scans each track for notes. Ignores if no notes found
//Uses _trackCount 
//Sets _trackCount, _tracks
uint8_t getMidiTracks()
{
	uint8_t cnt = 0;
	uint8_t usedTracks = 0;
	uint32_t trackStart = (uint32_t)MIDI_FIRST_TRACK_OFFSET + _fileAddressOffset + 4;// + 4 to skip the track header	
	uint32_t trackLen = 0;
    uint8_t *ptr = &trackLen;
	
	 while (cnt < _tractCount && usedTracks < MIDI_MAX_MIDI_TRACKS) 
	{
		//Clear the track data
		_tracks[usedTracks].eventByte = 0;
		_tracks[usedTracks].eventData1 = 0;
		_tracks[usedTracks].eventData2 = 0;
		_tracks[usedTracks].addressOffset = 0;
		_tracks[usedTracks].startAddress = 0;

        waitForNotBusy();

        CS_DISABLE();
        CS_ENABLE();
        sendSPI(MEM_READ);
        memSendAddress(trackStart);
            
		//Get the length of the track
		*(ptr + 3) = sendDummy();
		*(ptr + 2) = sendDummy();
		*(ptr + 1) = sendDummy();
		*ptr = sendDummy();
		
		trackStart += 4; //move the trackStart to after the length
		//set the start of the track
		_tracks[usedTracks].startAddress = trackStart;
		
		//scan the track for a starting note. If no note-on found, skip the track
		//Doing a continuous read for better performance
        if (getNextEvent(usedTracks) == 0xFF) //Checking 0xFF explicitly because there might be a 0x2F returned.
        {
            usedTracks++;
        }

        CS_DISABLE();

		//Update the trackStart here because we will need it later.
		trackStart += trackLen + 4;
				
		cnt++;
		
	}
	
	return usedTracks;
}

static inline void initMemory()
{

	getFileCount();
	_fileAddressOffset = FILE_COUNT_ADDR + 2 + (_fileCount * 4);
}

void loadFile(uint16_t index)
{
	_fileIndex = index;
	moveToFile();
	getTrackCount();
	getTimeDivision();
	_tractCount = getMidiTracks();
	_activeTracks = _tractCount;
}

//The following static inline methods are made inline so less stack space is used, better performance, and have no internal variables. They are broken out for readability. May have to remove inline if code space is needed

static void  startMidi()
{
	
    sendMidi(MIDI_START);
	sendMidi((_fileIndex >> 8));
	sendMidi(_fileIndex);
	sendMidi(MIDI_UPDATE_MODE);
	sendMidi(((_statusFlags >> 2) & 0x03));
    
	TIMER_ENABLE();
}

static void  continueMidi()
{
	sendMidi(MIDI_CONTINUE);
	TIMER_ENABLE();
}

static void  stopMidi()
{
	sendMidi(MIDI_STOP);
	TIMER_DISABLE();
}

static void  moveToNext()
{
	_fileIndex++;
	if (_fileIndex >= _fileCount)
	{
		_fileIndex = 0;
	}
			
	stopMidi(); //to stop the current notes that are playing
	loadFile(_fileIndex);
	startMidi();	
}

static void  moveToPrevious()
{
	if (_fileIndex == 0 || _fileIndex >= _fileCount)
	{
		_fileIndex = _fileCount - 1;
	}
	else
	{
		_fileIndex--;
	}
			
	stopMidi(); //to stop the current notes that are playing
	loadFile(_fileIndex);
	startMidi();	
}

//Three play modes: 0 = continuous play, 1 = repeat, 2 = stop after finished with current
//Updates bits 3:2 of the status
static void  updateMidiPlayMode()
{
	if ((_statusFlags & INPUT_PLAY_MODE_MASK) == PLAY_MODE_2_bm)
	{
		_statusFlags &= PLAY_MODE_CLEAR_MASK; //Set the play mode to 0
	}
	else if ((_statusFlags & INPUT_PLAY_MODE_MASK) == PLAY_MODE_1_bm)
	{
		//first reset to 0
		_statusFlags &= PLAY_MODE_CLEAR_MASK; //Clear the play mode bits
		_statusFlags |= PLAY_MODE_2_bm; //Set the play mode to 2
		
	}
	else if ((_statusFlags & INPUT_PLAY_MODE_MASK) == 0)
	{
		_statusFlags &= PLAY_MODE_CLEAR_MASK; //Clear the play mode bits
		_statusFlags |= PLAY_MODE_1_bm; //Set the play mode to 1
	}
	
	sendMidi(MIDI_UPDATE_MODE);
	sendMidi(((_statusFlags >> 2) & 0x03));
}

static void  handleClockTick()
{
	
	//0: update periods
	//1: send events from each track who's period has expired, if no periods have expired, return
	//2: get next event for each track
	//3: goto 1.
	uint8_t prevEventByte;
	uint8_t prevNote;
	uint8_t out = 0;

	for(uint16_t i = 0; i < _tractCount; i++)
	{

		//getNextEvent requires the memory to be in continuous read mode. This is an optimization to speed up reading the next event
		waitForNotBusy();	
		CS_DISABLE();
		CS_ENABLE();
		sendSPI(MEM_READ);
		memSendAddress(_tracks[i].startAddress + _tracks[i].addressOffset);
												
		//Get the next event and keep sending as long as the deltatime is 0										
		while(1)
		{	
			if (!_tracks[i].eventByte) //if there is no event, skip ahead
			{
				break;
			}
							
			if (!_tracks[i].deltaTime) //A DeltaTime has reached 0;
			{
					
				prevEventByte = _tracks[i].eventByte;
				prevNote = _tracks[i].eventData1;
				sendMidi(_tracks[i].eventByte);
				sendMidi(_tracks[i].eventData1);
				sendMidi(_tracks[i].eventData2);
				
				out = getNextEvent(i);
				if (out == 0)
				{
					break;
				}		
				
				if (out == 0x2F)
				{
					_activeTracks--;
					break;
				}
				if ((prevEventByte & 0xF0) == MIDI_NOTE_ON_MASK && (_tracks[i].eventByte & 0xF0) == MIDI_NOTE_OFF_MASK && _tracks[i].deltaTime == 0 && prevNote == _tracks[i].eventData1) // Skipping a note off if it was 0 length since the last.
				{

					out = getNextEvent(i);
					if (out == 0)
					{
						break;
					}
					
					if (out == 0x2F)
					{
						_activeTracks--;
						break;
					}
				}
				
										
			}
			else
			{
				break;
			}
		}
		CS_DISABLE();

		//Just to avoid overflowing deltatime (since it's a 32 bit number). This may actually be slower and it may not matter in the end
		if(_tracks[i].deltaTime)						
		{
			_tracks[i].deltaTime--;	
		}	
		
		//Here we are checking to see if any tracks are left playing and disabling the timer if there are none.
		//This also handles what to do next for play modes.
		if (!_activeTracks)
		{
			TIMER_DISABLE();
			//If mode 2, we just stop
			stopMidi();
			
			if ((_statusFlags & INPUT_PLAY_MODE_MASK) == PLAY_MODE_1_bm) //Loop mode
			{
				loadFile(_fileIndex);
				startMidi();						
			}
			else if ((_statusFlags & INPUT_PLAY_MODE_MASK) == 0) //Continuous mode
			{
				moveToNext();
			}
		}
	}
	
}

static void checkButtons()
{
				
	//NOTE: Counts are guaranteed not to roll over
	if (_input0Counts >= LONG_HOLD_INPUT_COUNT) //approx 1000ms hold
	{
		if (!(_statusFlags & INPUT_PLAY_MODE_UPDATING_MASK)) //If already in updating mode, do nothing. Basically if the button is still being held, don't try to update play mode again.
		{
			_statusFlags &= ~INPUT_BUTTON_0_MASK; //Clear the button 0 flag to ensure this does not allow the midi to become immediately unpaused
			_statusFlags |= INPUT_PLAY_MODE_UPDATING_MASK;
			updateMidiPlayMode();
		}
	}
	else if (_input0Counts > SHORT_HOLD_INPUT_COUNT && _input1Counts < LONG_HOLD_INPUT_COUNT) //approx 50ms hold
	{
		_statusFlags |= INPUT_BUTTON_0_MASK; //second bit indicate button 0 is pressed

	}
	else if (_input0Counts == 0)
	{
		if ((_statusFlags & INPUT_BUTTON_0_MASK) && !(_statusFlags & INPUT_PLAY_MODE_UPDATING_MASK)) //Only want to move forward or unpause if we didn't just release from a long button 0 hold to update play mode
		{
			if ((_statusFlags & INPUT_PAUSE_MASK)) //In pause mode and button was pressed and released quickly so unpause instead of moving forward
			{
				_statusFlags &= ~INPUT_PAUSE_MASK; //clear midi flag
				continueMidi();
			}
			else
			{
				moveToNext();
			}
		}
			
		//Always reset button down status
		_statusFlags &= ~INPUT_BUTTON_0_MASK; //Clear the button 0 flag
		//Always reset play mode update status
		_statusFlags &= ~INPUT_PLAY_MODE_UPDATING_MASK;
	}
		
		
		
		
	//NOTE: Counts are guaranteed not to roll over
	if (_input1Counts >= LONG_HOLD_INPUT_COUNT) //approx 1000ms hold
	{
		if (!(_statusFlags & INPUT_PAUSE_MASK))	//If already in pause mode, don't pause again
		{
			_statusFlags |= INPUT_PAUSE_MASK; //set pause status
			_statusFlags &= ~INPUT_BUTTON_1_MASK; //Clear the button 1 flag to ensure this does not allow the midi to become immediately unpaused
			stopMidi();
		}
	}
	else if (_input1Counts > SHORT_HOLD_INPUT_COUNT && _input1Counts < LONG_HOLD_INPUT_COUNT) //approx 50ms hold
	{
		_statusFlags |= INPUT_BUTTON_1_MASK; //second bit indicate button 1 is pressed

	}
	else if (_input1Counts == 0)
	{
		if ( (_statusFlags & INPUT_BUTTON_1_MASK))
		{
			if ((_statusFlags & INPUT_PAUSE_MASK)) //In pause mode and button was pressed and released quickly so unpause instead of moving forward
			{
				_statusFlags &= ~INPUT_PAUSE_MASK; //clear midi flag
				continueMidi();
			}
			else
			{
				moveToPrevious();
			}
		}
		//Always reset button down status
		_statusFlags &= ~INPUT_BUTTON_1_MASK; //Clear the button 1 flag
	}	
}

void main(void) {
    
    cli();
    initClock();

    initMIDIUART();
    initMemSPI();
    initInputADC();
        initTimers();
    initInterrupts();
    
	initMemory();
	
	_fileIndex = 0xFFFF; //so it rolls over to the correct file with the very first button click.
	
//#ifdef DEBUG
    
    _fileIndex = 2;
	stopMidi(); //to stop the current notes that are playing
	loadFile(_fileIndex);
	startMidi();    
//#else  
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {}  
//#endif
    
    while (1) 
    {
		if (_clockTickFlag)
		{
			handleClockTick();
			_clockTickFlag = 0x00;
		}			
		
//#ifndef	DEBUG	
        checkButtons();
//#endif
        
    }    
}

ISR(TCA0_OVF_vect)
{
     TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
 	_clockTickFlag = 0xFF; 
}

ISR(TCB0_INT_vect)
{
	uint8_t res;
//#ifdef DEBUG
//    res = 0;
//#else
	res = 	ADC0.RES;
//#endif
    
    TCB0.INTFLAGS = TCB_CAPTEI_bm;
    if (res > ADC_INPUT_0_WIN_LO && res < ADC_INPUT_0_WIN_HI)
	{
		
		if(_input0Counts < 0xFF)
		{
			_input0Counts++;
		}
		_input1Counts = 0;
	}
	else if (res >= ADC_INPUT_0_WIN_HI)
	{
		
		if(_input1Counts < 0xFF)
		{
		  _input1Counts++;
		}
		_input0Counts = 0;
	}
	else
	{
		_input0Counts = 0;
		_input1Counts = 0;	
    }
	
}