#include <xc.h>

// PIC16F15313 Configuration Bit Settings
#pragma config FEXTOSC = OFF
#pragma config RSTOSC = HFINT1
#pragma config CLKOUTEN = OFF
#pragma config CSWEN = ON
#pragma config FCMEN = OFF
#pragma config MCLRE = ON
#pragma config PWRTE = OFF
#pragma config LPBOREN = OFF
#pragma config BOREN = ON
#pragma config BORV = LO
#pragma config ZCD = OFF
#pragma config PPS1WAY = ON
#pragma config STVREN = ON
#pragma config WDTCPS = WDTCPS_31
#pragma config WDTE = OFF
#pragma config WDTCWS = WDTCWS_7
#pragma config WDTCCS = LFINTOSC
#pragma config WRT = OFF
#pragma config SCANE = available
#pragma config LVP = ON
#pragma config CP = OFF
#pragma config CPD = OFF

#define _XTAL_FREQ                  1000000UL

#define TICK_MS                     10UL
#define ADC_MAX_VALUE               1023U
#define INITIAL_ON_TIME_MS          1000UL
#define INITIAL_OFF_TIME_MS         300000UL
#define ON_TIME_MIN_MS              200UL
#define ON_TIME_MAX_MS              4000UL
#define OFF_TIME_MIN_MS             60000UL
#define OFF_TIME_MAX_MS             1800000UL
#define ADC_UPDATE_DIVIDER          10U      // 100 ms at a 10 ms scheduler tick

#define PWM1_TRIGGER_PORT           PORTAbits.RA2
#define PWM2_TRIGGER_PORT           PORTAbits.RA4
#define PWM1_OUTPUT_MASK            0x01U
#define PWM2_OUTPUT_MASK            0x02U

#define FALSE                       0U
#define TRUE                        1U

typedef struct
{
    unsigned long on_time_ms;
    unsigned long off_time_ms;
    unsigned long remaining_ms;
    unsigned char output_on;
    unsigned char trigger_latched;
} slow_pwm_t;

static volatile unsigned char g_tick_flag = FALSE;
static slow_pwm_t g_pwm1 = { INITIAL_ON_TIME_MS, INITIAL_OFF_TIME_MS, INITIAL_OFF_TIME_MS, FALSE, FALSE };
static slow_pwm_t g_pwm2 = { INITIAL_ON_TIME_MS, INITIAL_OFF_TIME_MS, INITIAL_OFF_TIME_MS, FALSE, FALSE };

static void oscillator_initialize(void);
static void port_initialize(void);
static void adc_initialize(void);
static void timer0_initialize(void);
static unsigned int adc_read(unsigned char channel);
static unsigned long scale_adc(unsigned int adc_value, unsigned long min_value, unsigned long max_value);
static void update_shared_timing(void);
static void force_pwm_on(volatile slow_pwm_t *pwm);
static void service_pwm(volatile slow_pwm_t *pwm, unsigned char output_mask);

void __interrupt() isr(void)
{
    if (PIR0bits.TMR0IF)
    {
        PIR0bits.TMR0IF = 0;
        TMR0H = 217;   // Reload for ~10 ms overflow at Fosc/4 = 250 kHz, prescaler 1:32
        TMR0L = 236;
        g_tick_flag = TRUE;
    }
}

static void oscillator_initialize(void)
{
    OSCFRQ = 0x00;            // 1 MHz HFINTOSC
}

static void port_initialize(void)
{
    ANSELA = 0b00000011;      // RA0/AN0 = ON control, RA1/AN1 = OFF control
    ANSELC = 0x00;

    TRISA = 0b00010111;       // RA0, RA1 analog inputs; RA2 and RA4 digital triggers; RA3 MCLR input
    TRISC = 0b00000000;       // RC0 and RC1 outputs

    WPUA = 0b00010100;        // weak pull-ups on trigger inputs
    OPTION_REGbits.nWPUEN = 0;

    LATA = 0x00;
    LATC = 0x00;
}

static void adc_initialize(void)
{
    ADCLK = 0x3F;
    ADREF = 0x00;             // VDD reference
    ADCON0 = 0x01;            // ADC enabled
    ADCON1 = 0x70;            // right-justified, FOSC/ADCLK
}

static void timer0_initialize(void)
{
    T0CON0 = 0b10010000;      // TMR0 on, 16-bit mode, no postscaler
    T0CON1 = 0b01000101;      // Fosc/4, prescaler 1:32, async disabled
    TMR0H = 217;
    TMR0L = 236;
    PIR0bits.TMR0IF = 0;
    PIE0bits.TMR0IE = 1;
}

static unsigned int adc_read(unsigned char channel)
{
    ADCON0bits.CHS = channel;
    __delay_us(20);
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO)
    {
        ;
    }

    return (((unsigned int)ADRESH) << 8) | ADRESL;
}

static unsigned long scale_adc(unsigned int adc_value, unsigned long min_value, unsigned long max_value)
{
    unsigned long range = max_value - min_value;
    return min_value + ((((unsigned long)adc_value) * range) / ADC_MAX_VALUE);
}

static void update_shared_timing(void)
{
    unsigned long on_time = scale_adc(adc_read(0), ON_TIME_MIN_MS, ON_TIME_MAX_MS);
    unsigned long off_time = scale_adc(adc_read(1), OFF_TIME_MIN_MS, OFF_TIME_MAX_MS);

    g_pwm1.on_time_ms = on_time;
    g_pwm2.on_time_ms = on_time;
    g_pwm1.off_time_ms = off_time;
    g_pwm2.off_time_ms = off_time;

    if (g_pwm1.output_on != FALSE)
    {
        if (g_pwm1.remaining_ms > g_pwm1.on_time_ms)
        {
            g_pwm1.remaining_ms = g_pwm1.on_time_ms;
        }
    }
    else if (g_pwm1.remaining_ms > g_pwm1.off_time_ms)
    {
        g_pwm1.remaining_ms = g_pwm1.off_time_ms;
    }

    if (g_pwm2.output_on != FALSE)
    {
        if (g_pwm2.remaining_ms > g_pwm2.on_time_ms)
        {
            g_pwm2.remaining_ms = g_pwm2.on_time_ms;
        }
    }
    else if (g_pwm2.remaining_ms > g_pwm2.off_time_ms)
    {
        g_pwm2.remaining_ms = g_pwm2.off_time_ms;
    }
}

static void force_pwm_on(volatile slow_pwm_t *pwm)
{
    pwm->output_on = TRUE;
    pwm->remaining_ms = pwm->on_time_ms;
}

static void service_pwm(volatile slow_pwm_t *pwm, unsigned char output_mask)
{
    if (pwm->remaining_ms > TICK_MS)
    {
        pwm->remaining_ms -= TICK_MS;
    }
    else
    {
        if (pwm->output_on == FALSE)
        {
            pwm->output_on = TRUE;
            pwm->remaining_ms = pwm->on_time_ms;
        }
        else
        {
            pwm->output_on = FALSE;
            pwm->remaining_ms = pwm->off_time_ms;
        }
    }

    if (pwm->output_on != FALSE)
    {
        LATC |= output_mask;
    }
    else
    {
        LATC &= (unsigned char)(~output_mask);
    }
}

void main(void)
{
    unsigned char adc_divider = 0;

    oscillator_initialize();
    port_initialize();
    adc_initialize();
    timer0_initialize();

    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;

    update_shared_timing();

    while (1)
    {
        if (g_tick_flag == FALSE)
        {
            continue;
        }

        g_tick_flag = FALSE;

        adc_divider++;
        if (adc_divider >= ADC_UPDATE_DIVIDER)
        {
            adc_divider = 0;
            update_shared_timing();
        }

        if (PWM1_TRIGGER_PORT == 0)
        {
            if (g_pwm1.trigger_latched == FALSE)
            {
                force_pwm_on(&g_pwm1);
                g_pwm1.trigger_latched = TRUE;
            }
        }
        else
        {
            g_pwm1.trigger_latched = FALSE;
        }

        if (PWM2_TRIGGER_PORT == 0)
        {
            if (g_pwm2.trigger_latched == FALSE)
            {
                force_pwm_on(&g_pwm2);
                g_pwm2.trigger_latched = TRUE;
            }
        }
        else
        {
            g_pwm2.trigger_latched = FALSE;
        }

        service_pwm(&g_pwm1, PWM1_OUTPUT_MASK);
        service_pwm(&g_pwm2, PWM2_OUTPUT_MASK);
    }
}
