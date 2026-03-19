#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define _XTAL_FREQ 1000000UL

// PIC16F15313 application behavior:
// - RA0 / AN0 sets the all-off delay before the output sequence begins.
// - RA1 / AN1 sets how long each output stays ON.
// - RC0, RC1, RC2, and RC3 are driven one at a time in sequence.
//
// Built for XC8/XC8++ style device headers. Adjust oscillator/config bits in
// your MPLAB X project as needed for your hardware.


namespace
{
    constexpr uint16_t ADC_MAX_COUNTS = 1023U;

    // Analog input assignments.
    constexpr uint8_t OFF_DELAY_CHANNEL = 0U; // RA0 / AN0
    constexpr uint8_t ON_TIME_CHANNEL   = 1U; // RA1 / AN1

    // Output assignments.
    #define OUT1 LATCbits.LATC0
    #define OUT2 LATCbits.LATC1
    #define OUT3 LATCbits.LATC2
    #define OUT4 LATCbits.LATC3

    constexpr uint16_t OFF_DELAY_MIN_MS = 250U;
    constexpr uint16_t OFF_DELAY_MAX_MS = 10000U;
    constexpr uint16_t ON_TIME_MIN_MS   = 100U;
    constexpr uint16_t ON_TIME_MAX_MS   = 3000U;

    inline uint16_t map_adc_to_range(uint16_t adc_value, uint16_t min_value, uint16_t max_value)
    {
        const uint32_t span = static_cast<uint32_t>(max_value - min_value);
        return static_cast<uint16_t>(min_value + ((static_cast<uint32_t>(adc_value) * span) / ADC_MAX_COUNTS));
    }

    void oscillator_init()
    {
        OSCFRQbits.HFFRQ = 0b001; // 1 MHz HFINTOSC
    }

    void gpio_init()
    {
        // Clear outputs first.
        LATA = 0x00;
        LATC = 0x00;

        // RA0 and RA1 as analog inputs.
        TRISAbits.TRISA0 = 1;
        TRISAbits.TRISA1 = 1;
        ANSELAbits.ANSA0 = 1;
        ANSELAbits.ANSA1 = 1;

        // RC0..RC3 as digital outputs.
        TRISCbits.TRISC0 = 0;
        TRISCbits.TRISC1 = 0;
        TRISCbits.TRISC2 = 0;
        TRISCbits.TRISC3 = 0;
        ANSELCbits.ANSC0 = 0;
        ANSELCbits.ANSC1 = 0;
        ANSELCbits.ANSC2 = 0;
        ANSELCbits.ANSC3 = 0;
    }

    void adc_init()
    {
        // Right-justified result, VDD/VSS references.
        ADCON0bits.FM = 1;
        ADCON0bits.CS = 1; // FOSC/ADCLK
        ADCLK = 0x3F;      // Slow enough for stable conversion at 1 MHz
        ADREF = 0x00;
        ADCON0bits.ON = 1;
    }

    uint16_t adc_read(uint8_t channel)
    {
        ADPCH = channel;
        __delay_us(10);

        ADCON0bits.GO = 1;
        while (ADCON0bits.GO)
        {
            ;
        }

        return (static_cast<uint16_t>(ADRESH) << 8) | ADRESL;
    }

    void delay_ms_blocking(uint16_t total_ms)
    {
        while (total_ms-- > 0U)
        {
            __delay_ms(1);
        }
    }

    void all_outputs_off()
    {
        OUT1 = 0;
        OUT2 = 0;
        OUT3 = 0;
        OUT4 = 0;
    }

    void pulse_output(uint8_t output_index, uint16_t on_time_ms)
    {
        all_outputs_off();

        switch (output_index)
        {
            case 0: OUT1 = 1; break;
            case 1: OUT2 = 1; break;
            case 2: OUT3 = 1; break;
            case 3: OUT4 = 1; break;
            default: return;
        }

        delay_ms_blocking(on_time_ms);
        all_outputs_off();
    }
}

void main(void)
{
    oscillator_init();
    gpio_init();
    adc_init();
    all_outputs_off();

    while (true)
    {
        const uint16_t off_delay_adc = adc_read(OFF_DELAY_CHANNEL);
        const uint16_t on_time_adc = adc_read(ON_TIME_CHANNEL);

        const uint16_t off_delay_ms = map_adc_to_range(off_delay_adc, OFF_DELAY_MIN_MS, OFF_DELAY_MAX_MS);
        const uint16_t on_time_ms = map_adc_to_range(on_time_adc, ON_TIME_MIN_MS, ON_TIME_MAX_MS);

        all_outputs_off();
        delay_ms_blocking(off_delay_ms);

        // Sequentially energize each output for the analog-programmed ON time.
        pulse_output(0, on_time_ms);
        pulse_output(1, on_time_ms);
        pulse_output(2, on_time_ms);
        pulse_output(3, on_time_ms);
    }
}
