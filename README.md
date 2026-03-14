TinyAVR402 DFPlayer bit-banged UART

Files:
- main.c : bit-banged UART TX/RX on PA6 (TX) and PA7 (RX); DFPlayer command helper
- Makefile: build with avr-gcc for attiny402

Notes:
- DFPlayer protocol: frame = 0x7E 0xFF 0x06 cmd feedback p1 p2 checksum_hi checksum_lo 0xEF
- The implementation is software UART; it uses blocking polling for RX. It is simple and portable but not interrupt-driven.

Wiring:
- Connect PA6 (TX from MCU) to RX of DFPlayer
- Connect PA7 (RX to MCU) to TX of DFPlayer
- Shared GND required


