/*
 * main.c - Conditional ADC on PA3 based on PA2 level, drive PA1 accordingly
 *
 * Notes:
 * - Adjust VCC_MV below to match your board supply (5000 for 5V, 3300 for 3.3V).
 * - This is bare-metal code for ATtiny402 using avr-gcc headers.
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#ifndef VCC_MV
#define VCC_MV 5000UL
#endif

#define THRESH_MV 2750UL
#define ADC_MAX 1023UL

int main(void)
{
    // Configure PA1 as output and drive it high initially
    PORTA.DIRSET = PIN1_bm;
    PORTA.OUTSET = PIN1_bm;

    // Configure PA2 as input with internal pull-up
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

    // Precompute threshold ADC counts for 2.75V
    uint16_t threshold = (THRESH_MV * ADC_MAX) / VCC_MV;

    for (;;)
    {
        // Read PA2 level
        uint8_t pa2_high = (PORTA.IN & PIN2_bm) != 0;

        // Start ADC conversion on PA3
        ADC0.COMMAND = ADC_STCONV_bm;
        // Wait for result ready
        while (!(ADC0.INTFLAGS & ADC_RESRDY_bm))
            ;
        uint16_t sample = ADC0.RES;

        if (pa2_high)
        {
            // If PA2 high, drive PA1 low if PA3 > 2.75V
            if (sample > threshold)
            {
                PORTA.OUTCLR = PIN1_bm;
            }
            else
            {
                PORTA.OUTSET = PIN1_bm;
            }
        }
        else
        {
            // If PA2 low, drive PA1 low if PA3 < 2.75V (opposite)
            if (sample < threshold)
            {
                PORTA.OUTCLR = PIN1_bm;
            }
            else
            {
                PORTA.OUTSET = PIN1_bm;
            }
        }

        _delay_ms(100);
    }

    return 0;
}
