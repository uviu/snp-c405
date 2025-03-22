#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sleep.h>

volatile uint8_t seconds = 50;    // sekundenzähler
volatile uint8_t minutes = 30;    // minutenzähler
volatile uint8_t hours = 12;      // stundenzähler
volatile uint8_t brightness_stage = 0;  // helligkeitsstufe
volatile uint8_t enter_sleep_flag = 0;  // schlafmodus flag

const uint8_t brightness_levels[] = {254, 248, 156, 0};  // helligkeitsstufen

// timer2 für zeiterfassung konfigurieren (1 Hz interrupt)
void setup_timer2() {
    ASSR |= (1 << AS2);
    TCCR2A = 0;
    TCCR2B = (1 << CS22) | (1 << CS20);
    TIMSK2 = (1 << TOIE2);
    while (ASSR & ((1 << TCR2BUB) | (1 << TCR2AUB) | (1 << OCR2AUB) | (1 << TCN2UB)));
}

// timer1 für pwm an pb1 und pb2 initialisieren
void setup_timer1() {
    TCCR1A = (1 << WGM10) | (1 << COM1A1) | (1 << COM1B1);
    TCCR1B = (1 << WGM12) | (1 << CS11);
    OCR1A = brightness_levels[brightness_stage];
    OCR1B = brightness_levels[brightness_stage];
}

void setup_ports() {
    DDRC |= 0x3F;        // pc0-pc5 als ausgänge für minutendisplay
    DDRD |= (1 << PD0) | (1 << PD1) | (1 << PD5) | (1 << PD6) | (1 << PD7);  // stundendisplay
    DDRD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4));  // taster als eingänge
    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);    // pull-up widerstände aktivieren
    DDRB |= (1 << PB1) | (1 << PB2);  // pwm ausgänge
}

void setup_external_interrupts() {
    EICRA |= (1 << ISC01) | (1 << ISC11);  // interrupt bei fallender flanke
    EIMSK |= (1 << INT0) | (1 << INT1);     // int0 und int1 aktivieren
}

// anzeige mit aktueller zeit aktualisieren
void update_display() {
    PORTC = (PORTC & ~0x3F) | (minutes & 0x3F);  // minuten anzeigen

    uint8_t hr = hours & 0x1F;
    uint8_t hour_output = ((hr & 0x01) << 7) | ((hr & 0x02) << 5) | ((hr & 0x04) << 3) |
                         ((hr & 0x08) >> 2) | ((hr & 0x10) >> 4);
    PORTD = (PORTD & ~0xE3) | hour_output;  // stunden anzeigen
}

// timer2 overflow interrupt (1 sekunde)
ISR(TIMER2_OVF_vect) {
    seconds++;
    if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) hours = 0;
        }
    }

    // helligkeitstaste abfragen (pd4)
    if (!(PIND & (1 << PD4))) {
        _delay_ms(5);
        if (!(PIND & (1 << PD4))) {
            brightness_stage = (brightness_stage + 1) % 4;
            if (brightness_stage == 0) enter_sleep_flag = 1;

            OCR1A = brightness_levels[brightness_stage];
            OCR1B = brightness_levels[brightness_stage];
            while (!(PIND & (1 << PD4)));
            _delay_ms(5);
        }
    }
    if (!enter_sleep_flag) update_display();
}

// interrupt service routine für minutentaster (int0)
ISR(INT0_vect) {
    _delay_ms(5);
    if (!(PIND & (1 << PD2))) {
        minutes++;
        seconds = 0;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) hours = 0;
        }
        update_display();
    }
}

// interrupt service routine für stundentaster (int1)
ISR(INT1_vect) {
    _delay_ms(5);
    if (!(PIND & (1 << PD3))) {
        hours++;
        if (hours >= 24) hours = 0;
        update_display();
    }
}

// pin-change interrupt für pd4 (aufwecken aus schlafmodus)
ISR(PCINT2_vect) {
    enter_sleep_flag = 0;
}

int main(void) {
    setup_ports();
    setup_timer2();
    setup_timer1();
    setup_external_interrupts();

    // pin-change interrupt für pd4 initial deaktivieren
    PCICR &= ~(1 << PCIE2);
    PCMSK2 &= ~(1 << PCINT20);

    sei();  // globale interrupts aktivieren

    while (1) {
        if (enter_sleep_flag) {
            // pwm aussetzen und displays abschalten
            OCR1A = 255;
            OCR1B = 255;

            // pd4 pin-change interrupt für aufwecken aktivieren
            PCICR |= (1 << PCIE2);
            PCMSK2 |= (1 << PCINT20);

            // stromsparmodus aktivieren (power-save) !deaktiviert internen takt
            set_sleep_mode(SLEEP_MODE_PWR_SAVE);
            sleep_mode();

            // interrupt nach aufwachen deaktivieren
            PCICR &= ~(1 << PCIE2);
            PCMSK2 &= ~(1 << PCINT20);

            // helligkeit zurücksetzen und displays aktivieren
            brightness_stage = 0;
            OCR1A = brightness_levels[brightness_stage];
            OCR1B = brightness_levels[brightness_stage];
        }else{
            // energiesparmodus zwischen interrupts (idle)
            set_sleep_mode(SLEEP_MODE_IDLE);
            sleep_mode();
        }
    }
    return 0;
}
