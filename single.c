/*
 * Control routine for single_sensor board
 *  - Based on the analog level of the PA3 pin play a track on DFPlayer Mini when
 *    that voltages crosses a threshold. The direction of crossing is controlled by PA2
 *  - Flash an LED for various functions and as a heartbeat
 *
 * Bit-banged UART to talk to the DFPlayer Mini
 *
 * This is bare-metal code for ATtiny402 using avr-gcc headers and is not based on the Arduino IDE
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

// Set VCC_MV to match your board supply (5000 for 5V, 3300 for 3.3V).
#ifndef VCC_MV
#define VCC_MV 5000UL
#endif

// Misc ADC constants
#define ADC_MAX 1023UL

/*
 * The threshold between light and dark. For the 5516 photocell
 *  with typical room lighting (1-2K light and 5-10K dark) 
 *  and the value of the resistors in the divider (3K to Vcc)
 *
 * For other photocells and/or other lighting conditions this will differ
 */
#define THRESH_MV 2750UL		// The voltage between light and dark

#define	THRESHOLD	 ((uint16_t) ((THRESH_MV * ADC_MAX) / VCC_MV))

// Perform a conversion on the ADC, here always PA3
//  and compare it to the threshold value we've chosen
static bool read_adc(void)
{
	// Start ADC conversion on PA3
	ADC0.COMMAND = ADC_STCONV_bm;
	
	// Wait for result ready
	while (!(ADC0.INTFLAGS & ADC_RESRDY_bm))
	    ;
	uint16_t sample = ADC0.RES;		// Make sure all the bits are read
	 
	// true if the current analog value is above the threshold
	return(sample > THRESHOLD);
}

/*
 * There is an AVR_controlled LED which is used for heartbeat and
 *  diagnostic functions.
 */
static void led_on(void) {
    PORTA.OUTCLR = (1<<1);
}
static void led_off(void) {
    PORTA.OUTSET = (1<<1);
}

#define	HALF_CYCLE	200
static void flash_led(uint8_t count_flash) {
	uint8_t i;
	
	for(i=0; i<count_flash; ++i)
	{
    led_on();
    _delay_ms(HALF_CYCLE);
		led_off();
		_delay_ms(HALF_CYCLE);		
	}	
}


/*
 * Serial interface
 *
 * This version of the software uses a bit-bang UART instead
 *  of the hardware in the AVR. Maybe a future version of the code
 *  will use that hardware.
 */

// Pin definitions (PA6 = TX, PA7 = RX)  use device-pack macros
#define TX_PIN_bm PIN6_bm
#define RX_PIN_bm PIN7_bm

// Serial parameters
#define BAUD 9600
// bit time in microseconds (rounded)
#define BIT_US (1000000UL / BAUD)

// Simple blocking bit-banged UART TX
static void uart_init_pins(void) {
	
    // TX output, drive high (idle)
    PORTA_DIRSET = TX_PIN_bm;
    PORTA_OUTSET = TX_PIN_bm;

    // RX input with pull-up enabled (clear DIR, set OUT for pull-up)
    PORTA_DIRCLR = RX_PIN_bm;
    PORTA_OUTSET = RX_PIN_bm;
}

static void uart_tx_byte(uint8_t b) {
    uint8_t i;
    
    // start bit (low)
    PORTA_OUTCLR = TX_PIN_bm;
    _delay_us(BIT_US);

    // data bits LSB first
    for (i = 0; i < 8; ++i) {
        if (b & (1 << i)) PORTA_OUTSET = TX_PIN_bm;
        else PORTA_OUTCLR = TX_PIN_bm;
        _delay_us(BIT_US);
    }

    // stop bit (high)
    PORTA_OUTSET = TX_PIN_bm;
    _delay_us(BIT_US);
}

// Blocking receive: waits for start bit, samples in middle of bit
// Returns 0 on timeout (if timeout_us==0, waits indefinitely)
static uint8_t uart_rx_byte(uint32_t timeout_us) {
    uint32_t waited = 0;

    // wait for start bit (line goes low)
    while (PORTA_IN & RX_PIN_bm) {
        if (timeout_us && (waited >= timeout_us)) return 0;
        _delay_us(10);
        waited += 10;
    }

    // found falling edge, wait half bit to sample center
    _delay_us(BIT_US / 2);

    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        _delay_us(BIT_US);
        if (PORTA_IN & RX_PIN_bm) b |= (1 << i);
    }

    // wait stop bit time
    _delay_us(BIT_US);
    return b;
}

/*
 * The audio module for this board is the DFPlayer Mini,
 *  originally created by DFRobot and still sold by them but
 *  also available as a wide range of clones, some of which are better
 *  than others!
 */
#define	PLAY_TRACK_CMD		0x03		// Param is track number
#define	VOLUME_UP 				0x04
#define	VOLUME_DOWN				0x05
#define	VOLUME_CMD				0x06		// Param is volume 0-30
#define	REPEAT_TRACK_CMD	0x08		// Param is track number
#define	STOP_CMD					0x16
#define	QUERY_VOLUME_CMD	0x43		// Returns the current volume 0-30

#define	QUERY_STATUS_CMD	0x42 		// Returns one of the 4 values below
#define	QUERY_STATUS_BYTE	6		   // Index 8 (7th byte) is the useful status
#define	QUERY_STOPPED			0
#define	QUERY_PLAYING			1
#define	QUERY_BUSY  			4			// such as initialization

#define	RESP_SIZE					10		// All responses should be this long

// Not exactly sure just what the feedback byte is for. Every example I have seen
//  sets this byte to 0 so that is fixed in this software
#define FEEDBACK          1    // feedback requested
#define NO_FEEDBACK       0    // no feedback requested
/*
 * Send a frame (cmd) to the DFPlayer Mini
 *
 * frame: 0x7E 0xFF 0x06 cmd feedback param1 param2 checksum_hi checksum_lo 0xEF
 */
static void dfplayer_send_cmd(uint8_t cmd, uint8_t p1, uint8_t p2) {
    uint16_t sum = 0;
    uint8_t frame[10];
    
    frame[0] = 0x7E;
    frame[1] = 0xFF;
    frame[2] = 0x06;
    frame[3] = cmd;
    frame[4] = NO_FEEDBACK;
    frame[5] = p1;
    frame[6] = p2;

    sum = frame[1] + frame[2] + frame[3] + frame[4] + frame[5] + frame[6];
    uint16_t checksum = 0xFFFF - sum + 1;
    frame[7] = (uint8_t)(checksum >> 8);
    frame[8] = (uint8_t)(checksum & 0xFF);
    frame[9] = 0xEF;

    for (uint8_t i = 0; i < 10; ++i) uart_tx_byte(frame[i]);
}

// Read DFPlayer response into provided buffer (max_len). Returns number of bytes read.
// Waits up to timeout_ms for first byte; after first byte, waits 50ms between bytes.
static uint8_t dfplayer_read_resp(uint8_t *buf, uint8_t max_len, uint32_t timeout_ms) {
    uint8_t idx = 0;
    uint32_t waited = 0;

    // wait for first byte
    while ((PORTA_IN & RX_PIN_bm) && (waited < (timeout_ms * 1000UL))) {
        _delay_us(100);
        waited += 100;
    }
    if (waited >= (timeout_ms * 1000UL)) return 0;

    // read until end byte 0xEF or buffer full
    while (idx < max_len) {
        uint8_t v = uart_rx_byte(200000); // 200ms per byte timeout
        //if (v == 0) break;
        buf[idx++] = v;
        if (v == 0xEF) break;
    }
    return idx;
}

/* 
 * Get DFPlayer status (send he Query_Status command)
 *   In general, we expect to wait between queries but in some
 *   situations might not want the wait
 */
static uint8_t dfplayer_get_status(uint32_t wait_ms) {
		uint8_t resp_buf[16];		// should only ever be 10 bytes...

    // Query status to see when it's done playing or initializeing
    while(1) {
			_delay_ms(wait_ms);
	    dfplayer_send_cmd(QUERY_STATUS_CMD, 0x00, 0x00);		// status query

	    // read response
	    uint8_t n = dfplayer_read_resp(resp_buf, sizeof(resp_buf), 500);
	   	if (n != RESP_SIZE)
	   		{ flash_led(2); continue;	} // try again on a failure
	   		
	   	return(resp_buf[QUERY_STATUS_BYTE]);	// Para2 is the only byte we care about
	  }
}

// Pin initializations
static void init_avr(void)
{
    uint8_t	timeout;
    
    // set clock rate to 20Mhz (assuming fuse 0x02 is set to 2)
    //  This must match the compile-time variable
		_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0); 
		
		// Configure PA1 (LED) as output and drive it high initially
    PORTA.DIRSET = PIN1_bm;
    led_off();

    // Configure PA2 as input with internal pull-up - the state of this pin
    //  determines the direction of the function performed:
    //   PA2 = high or open - play music for a high going transition (light->dark)
    //   PA2 = low          - play music for a low going transition (dark->light)
    PORTA.DIRCLR = PIN2_bm;
    PORTA.PIN2CTRL = PORT_PULLUPEN_bm;

    // Ensure PA3 is input (analog)
    PORTA.DIRCLR = PIN3_bm;

    // ADC setup:
    // - Reference: VDD (VDD as ADC reference)
    // - Prescaler: DIV4 (adjust if needed)
    ADC0.CTRLC = ADC_PRESC_DIV4_gc | ADC_REFSEL_VDDREF_gc;
    // Select PA3 as ADC positive input
    ADC0.MUXPOS = ADC_MUXPOS_AIN3_gc;
    // Enable ADC
    ADC0.CTRLA = ADC_ENABLE_bm;

    // Small settling delay
    _delay_ms(2);
    
    // Initialize the UART pins
    uart_init_pins();
    
    flash_led(1);	// Startup signal

		// let DFPlayer power up. Could take from 1-5 seconds
		for(timeout=0; dfplayer_get_status(500) == QUERY_BUSY; timeout++)
		{
			// This should be replaced with a watchdog timer function to reset
			//  the AVR as it does not have a reset instruction per se
			if (timeout > 10)
			{
				while(1)	flash_led(1);
			}
		}
}

int main(void)
{
	uint8_t prev_level;	// The previous level of the ADC, look for transition from this
	uint8_t cur_level;	// The current level of the ADC
	bool		play_track;	// Should the track be played during this iteration of the loop
	
	// All the initialization functions are gathered in here
	init_avr();
	
	// Read PA2 level. This isn't intended to be dynamic, rather a jumper
	//  or left open - so we read it once only
  uint8_t pa2_high = (PORTA.IN & PIN2_bm) != 0;
   
  // Loop endlessly playing the track when we get the desired transition
  //   There is no noise suppression in software at this time. The hardware has
  //   a time constant of ~10ms on the input which may be sufficient     
	for(prev_level=read_adc(); 1; prev_level=cur_level)
	{
		cur_level=read_adc();
		play_track = false;
		
		if (pa2_high) {
		    if (!prev_level && cur_level)
		    	play_track = true;
		}
		else {
		    if (prev_level && !cur_level)
		    	play_track = true;
		}
		
		// If the desired transition occurred, play the track and wait until
		//   the playback has finished before going back into the main loop
		if (play_track) {
	    // Send command to play track 1 (command 0x03, params hi/lo = 0x00 0x01)
	    dfplayer_send_cmd(PLAY_TRACK_CMD, 0x00, 0x01);
	    
	    // Query status to see when it's done playing
			while(dfplayer_get_status(500) == QUERY_PLAYING) {    	
		    flash_led(1);
	    }			
		}
		
		// Wait 1/10 of a second and try again
		else {
		_delay_ms(100);
		 }
  }
	
	// Should never get here. What does the AVR do?
  return 0;
}
