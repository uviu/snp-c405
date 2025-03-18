/*
  Revised Binary Clock Code for New Pin Assignments
  Buttons:
    - PD2 (INT0): Minutes button (interrupt driven)
    - PD3 (INT1): Hours button (interrupt driven)
    - PD4: Brightness button (polled)
  Cathodes:
    - PB1: Minutes cathodes (PWM output)
    - PB2: Hours cathodes (PWM output)
  Displays:
    - Hours (5 bits): PD0, PD1, PD5, PD6, PD7 
      (Mapping: bit0→PD0, bit1→PD1, bit2→PD5, bit3→PD6, bit4→PD7)
    - Minutes (6 bits): PC0 to PC5
  XTAL:
    - PB6 and PB7 for the crystal oscillator
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sleep.h>

volatile uint8_t seconds = 50;
volatile uint8_t minutes = 30;
volatile uint8_t hours = 12;
volatile uint8_t brightness_stage = 0;

const uint8_t brightness_levels[] = {254, 248, 156, 0}; // Brightness levels

// Timer2 is used for timekeeping (adjust as needed)
void setup_timer2() {
    ASSR |= (1 << AS2);
    // Activate asynchronous mode if needed; using internal clock here
    TCCR2A = 0;                      // Normal mode
    TCCR2B = (1 << CS22) | (1 << CS20); // Prescaler = 128
    TIMSK2 = (1 << TOIE2);           // Enable Timer2 Overflow interrupt
    // (If using an external clock, wait for clock stabilization)
    // Wait for the external clock to stabilize
    while (ASSR & ((1 << TCR2BUB) | (1 << TCR2AUB) | (1 << OCR2AUB) | (1 << TCN2UB))) {
      // Wait until all update busy flags are cleared
    }
}

// Timer1 drives PWM on PB1 (minutes cathodes) and PB2 (hours cathodes)
void setup_timer1() {
    TCCR1A = (1 << WGM10) | (1 << COM1A1) | (1 << COM1B1); // 8-bit Fast PWM, clear on compare match
    TCCR1B = (1 << WGM12) | (1 << CS11); // Prescaler = 8

    OCR1A = brightness_levels[brightness_stage]; // For minutes cathodes (PB1)
    OCR1B = brightness_levels[brightness_stage]; // For hours cathodes (PB2)
}

void setup_ports() {
    // --- Minutes Display (6 bits on Port C: PC0 - PC5) ---
    DDRC |= 0x3F;  // Set PC0 to PC5 as outputs

    // --- Hours Display (5 bits on Port D: PD0, PD1, PD5, PD6, PD7) ---
    // Set PD0, PD1, PD5, PD6, PD7 as outputs.
    DDRD |= (1 << PD0) | (1 << PD1) | (1 << PD5) | (1 << PD6) | (1 << PD7);
    // --- Buttons on Port D ---
    // PD2 and PD3 will be used with external interrupts; PD4 will be polled.
    DDRD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4));   // Set PD2, PD3, PD4 as inputs
    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);       // Enable pull-up resistors

    // --- PWM Outputs for Cathodes on Port B ---
    // PB1 for minutes, PB2 for hours.
    DDRB |= (1 << PB1) | (1 << PB2);
    // PB6 and PB7 remain dedicated to the crystal oscillator (do not change them)
}

// Configure external interrupts for the two buttons on PD2 and PD3.
void setup_external_interrupts() {
    // INT0 (PD2) and INT1 (PD3) triggered on falling edge.
    EICRA |= (1 << ISC01); // INT0: falling edge
    EICRA |= (1 << ISC11); // INT1: falling edge
    EIMSK |= (1 << INT0) | (1 << INT1);
}

// The display update function must remap the hour bits appropriately.
// Hours value (5 bits: b0..b4) will be mapped as follows:
//   bit0 -> PD0, bit1 -> PD1, bit2 -> PD5, bit3 -> PD6, bit4 -> PD7.
void update_display() {
    // --- Minutes Display on Port C ---
    PORTC = (PORTC & ~0x3F) | (minutes & 0x3F);

    // --- Hours Display on Port D ---
    uint8_t hr = hours & 0x1F;  // Ensure we use only 5 bits.
    uint8_t hour_output = (hr & 0x03)             // Bits 0 and 1 go to PD0 and PD1
                        | ((hr & 0x04) << 3)        // Bit 2 moves to PD5 (shift left 3: 2 -> 5)
                        | ((hr & 0x08) << 3)        // Bit 3 moves to PD6 (3 -> 6)
                        | ((hr & 0x10) << 3);       // Bit 4 moves to PD7 (4 -> 7)
    // Mask for hours output bits: PD0, PD1, PD5, PD6, PD7 (binary 11100011 = 0xE3)
    PORTD = (PORTD & ~0xE3) | hour_output;
}

// Timer2 Overflow ISR: updates time and polls the brightness button (PD4)
ISR(TIMER2_OVF_vect) {
    seconds++;
    if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24)
                hours = 0;
        }
    }
    // --- Poll the Brightness Button on PD4 ---
    if (!(PIND & (1 << PD4))) { // Active low: button pressed
        _delay_ms(5);         // Simple debounce
        if (!(PIND & (1 << PD4))) {
            // Cycle brightness level
            brightness_stage = (brightness_stage + 1) % 4;
            OCR1A = brightness_levels[brightness_stage];
            OCR1B = brightness_levels[brightness_stage];
            // Wait for release to avoid multiple triggers
            while (!(PIND & (1 << PD4)));
            _delay_ms(5);
        }
    }
    update_display();
}

// External Interrupt for PD2 (INT0): Minutes Button
ISR(INT0_vect) {
    _delay_ms(5); // Debounce
    if (!(PIND & (1 << PD2))) { // Confirm button is still pressed
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

// External Interrupt for PD3 (INT1): Hours Button
ISR(INT1_vect) {
    _delay_ms(5); // Debounce
    if (!(PIND & (1 << PD3))) { // Confirm button is still pressed
        hours++;
        if (hours >= 24)
            hours = 0;
        update_display();
    }
}

// Optionally, use sleep mode to save power.
void enter_sleep() {
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}

int main(void) {
    setup_ports();
    setup_timer2();
    setup_timer1();
    setup_external_interrupts();

    sei();  // Enable global interrupts

    while (1) {
        enter_sleep(); // Wait for an interrupt (time tick or button press)
        _delay_ms(5);  // Optional debounce or wake-up delay
    }

    return 0;
}
