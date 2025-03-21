/*
  Revised Binary Clock Code with Deep Sleep Mode on Brightness Button
  Buttons:
    - PD2 (INT0): Minutes button
    - PD3 (INT1): Hours button
    - PD4: Brightness button (now with deep sleep)
  Cathodes:
    - PB1: Minutes (PWM)
    - PB2: Hours (PWM)
  Displays:
    - Hours: PD0, PD1, PD5, PD6, PD7
    - Minutes: PC0-PC5
  Crystal: PB6/PB7
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sleep.h>

volatile uint8_t seconds = 50;
volatile uint8_t minutes = 30;
volatile uint8_t hours = 12;
volatile uint8_t brightness_stage = 0;
volatile uint8_t enter_sleep_flag = 0; // Flag to trigger sleep mode

const uint8_t brightness_levels[] = {254, 248, 156, 0}; // Brightness levels (4 stages)

// Timer2 for timekeeping (1 Hz interrupt)
void setup_timer2() {
    ASSR |= (1 << AS2); // Asynchronous mode (external crystal)
    TCCR2A = 0; // Normal mode
    TCCR2B = (1 << CS22) | (1 << CS20); // Prescaler 128
    TIMSK2 = (1 << TOIE2); // Enable overflow interrupt
    while (ASSR & ((1 << TCR2BUB) | (1 << TCR2AUB) | (1 << OCR2AUB) | (1 << TCN2UB)));
}

// Timer1 for PWM on PB1 and PB2
void setup_timer1() {
    TCCR1A = (1 << WGM10) | (1 << COM1A1) | (1 << COM1B1); // Fast PWM, non-inverting
    TCCR1B = (1 << WGM12) | (1 << CS11); // Prescaler 8
    OCR1A = brightness_levels[brightness_stage]; // Initial brightness
    OCR1B = brightness_levels[brightness_stage];
}

void setup_ports() {
    DDRC |= 0x3F; // PC0-PC5 as outputs (minutes)
    DDRD |= (1 << PD0) | (1 << PD1) | (1 << PD5) | (1 << PD6) | (1 << PD7); // Hours
    DDRD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4)); // Buttons as inputs
    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4); // Enable pull-ups
    DDRB |= (1 << PB1) | (1 << PB2); // PWM outputs
}

void setup_external_interrupts() {
    EICRA |= (1 << ISC01) | (1 << ISC11); // INT0/1 on falling edge
    EIMSK |= (1 << INT0) | (1 << INT1); // Enable interrupts
}

// Update displays with current time
void update_display() {
    PORTC = (PORTC & ~0x3F) | (minutes & 0x3F); // Minutes

    uint8_t hr = hours & 0x1F;
    uint8_t hour_output = ((hr & 0x01) << 7) | ((hr & 0x02) << 5) | ((hr & 0x04) << 3) |
                         ((hr & 0x08) >> 2) | ((hr & 0x10) >> 4);
    PORTD = (PORTD & ~0xE3) | hour_output; // Mask PD0, PD1, PD5-PD7
}

// Timer2 overflow ISR (1 Hz)
ISR(TIMER2_OVF_vect) {
    //switch when messuring via counter
    seconds++;
    //minutes++;
    if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) hours = 0;
        }
    }

    // Poll brightness button (PD4)
    if (!(PIND & (1 << PD4))) {
        _delay_ms(5);
        if (!(PIND & (1 << PD4))) {
            brightness_stage = (brightness_stage + 1) % 4; // Cycle 0-3

            // If cycled to 0 (after 3), trigger sleep
            if (brightness_stage == 0) enter_sleep_flag = 1;

            OCR1A = brightness_levels[brightness_stage];
            OCR1B = brightness_levels[brightness_stage];
            while (!(PIND & (1 << PD4))); // Wait for release
            _delay_ms(5);
        }
    }
    if (!enter_sleep_flag) update_display();
}

// INT0 (minutes) and INT1 (hours) handlers
ISR(INT0_vect) {
    _delay_ms(5);
    if (!(PIND & (1 << PD2))) {
        minutes++;
        seconds = 0;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24)
                hours = 0;
        }
        update_display();
    }
}

ISR(INT1_vect) {
    _delay_ms(5);
    if (!(PIND & (1 << PD3))) {
        hours++;
        if (hours >= 24) hours = 0;
        update_display();
    }
}

// Pin change ISR for PD4 (wake from sleep)
ISR(PCINT2_vect) {
    // No action needed; just wakes the MCU
    enter_sleep_flag = 0;
}

int main(void) {
    setup_ports();
    setup_timer2();
    setup_timer1();
    setup_external_interrupts();

    // Initially disable PCINT for PD4
    PCICR &= ~(1 << PCIE2);
    PCMSK2 &= ~(1 << PCINT20);

    sei();

    while (1) {
        if (enter_sleep_flag) {

            // Turn off displays
            OCR1A = 255;
            OCR1B = 255;

            // Enable PD4 pin change interrupt
            PCICR |= (1 << PCIE2);
            PCMSK2 |= (1 << PCINT20);

            // Enter deep sleep
            set_sleep_mode(SLEEP_MODE_PWR_SAVE);
            sleep_mode();

            // Disable PD4 interrupt
            PCICR &= ~(1 << PCIE2);
            PCMSK2 &= ~(1 << PCINT20);

            // Restore brightness
            brightness_stage = 0;
            OCR1A = brightness_levels[brightness_stage];
            OCR1B = brightness_levels[brightness_stage];
        }else{
            // Enter idle sleep between interrupts
            set_sleep_mode(SLEEP_MODE_IDLE);
            sleep_mode();
        }
    }
    return 0;
}
