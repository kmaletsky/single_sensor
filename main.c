/*
 * Talk to DFplayer with attiny402 and return status. Just a sample action at this time.
 *   Later will add pin inputs to drive specific commands
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

// Pin definitions (PA6 = TX, PA7 = RX) — use device-pack macros
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

// DFPlayer mini serial frame helper
// frame: 0x7E 0xFF 0x06 cmd feedback param1 param2 checksum_hi checksum_lo 0xEF
static void dfplayer_send_cmd(uint8_t cmd, uint8_t feedback, uint8_t p1, uint8_t p2) {
    uint16_t sum = 0;
    uint8_t frame[10];
    frame[0] = 0x7E;
    frame[1] = 0xFF;
    frame[2] = 0x06;
    frame[3] = cmd;
    frame[4] = feedback;
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
        if (v == 0) break;
        buf[idx++] = v;
        if (v == 0xEF) break;
    }
    return idx;
}

int main(void) {
    // Initialize pins
    uart_init_pins();

    _delay_ms(100); // let DFPlayer power up

    // Example: send command to play track 1 (command 0x03, params hi/lo = 0x00 0x01)
    dfplayer_send_cmd(0x03, 0x00, 0x00, 0x01);

    // read response
    uint8_t buf[16];
    uint8_t n = dfplayer_read_resp(buf, sizeof(buf), 500);

    // Echo any response back to TX (for debugging)
    for (uint8_t i = 0; i < n; ++i) {
        uart_tx_byte(buf[i]);
    }

    // Main loop: send simple heartbeat every 5s and read responses
    while (1) {
        _delay_ms(5000);
        dfplayer_send_cmd(0x0E, 0x00, 0x00, 0x00); // query status (example)
        n = dfplayer_read_resp(buf, sizeof(buf), 500);
        for (uint8_t i = 0; i < n; ++i) uart_tx_byte(buf[i]);
    }

    return 0;
}
