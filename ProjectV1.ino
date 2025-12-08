/*
 * Project: CPS Sensor Node (Pressure & Temp Edition)
 * Hardware: Arduino Uno (ATmega328P)
 * Sensoren: 
 * - Grove Light (A1)
 * - BMP280 (I2C) -> Meet Temperatuur & Druk (GEEN Luchtvochtigheid!)
 * Actuator: Ventilator (Pin 5)
 *
 * COMMANDO'S: 't'=20C, 'y'=30C, 'r'=Reset
 */

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

// --- Instellingen ---
#define BMP280_ADDR     0x76    
#define MAX_LUMEN       1000    
#define NODE_ID         1       

// Ventilator
#define TEMP_RANGE      500     
#define FAN_MIN_PWM     60      
#define FAN_MAX_PWM     255     

// --- Globale Variabelen ---
volatile uint8_t timer_tick = 0;
volatile uint32_t timestamp_counter = 0;
int32_t temp_threshold = 2500;           

// Globale variabele voor temperatuur-correctie (nodig voor druk!)
int32_t t_fine; 

// Kalibratie variabelen BMP280 (Temp & Pressure)
uint16_t dig_T1;
int16_t  dig_T2, dig_T3;
uint16_t dig_P1;
int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

// --- Drivers Prototypes ---
void UART_Init(void);
void UART_TxString(const char* str);
void UART_SendInt(uint16_t num);
void UART_SendLong(uint32_t num);
char UART_ReadChar_NonBlocking(void); 
void ADC_Init(void);
uint16_t ADC_Read(uint8_t channel);
void PWM_Init(void);
void PWM_SetDuty(uint8_t duty);
void Timer1_Init(void);
void I2C_Init(void);
void I2C_Start(void);
void I2C_Stop(void);
void I2C_Write(uint8_t data);
uint8_t I2C_ReadACK(void);
uint8_t I2C_ReadNACK(void);
void BMP280_Init(void);
int32_t BMP280_ReadRawTemp(void);
int32_t BMP280_ReadRawPress(void);
int32_t BMP280_Compensate_T(int32_t adc_T);
uint32_t BMP280_Compensate_P(int32_t adc_P);
void BMP280_ReadTrimming(void);

// --- MAIN ---
int main(void) {
    UART_Init();
    ADC_Init();
    PWM_Init();
    Timer1_Init();
    I2C_Init();

    _delay_ms(100);
    BMP280_Init();
    BMP280_ReadTrimming(); // Lees ALLE calibratie data (T en P)

    UART_TxString("\r\n--- CPS SYSTEM READY (Temp + Pressure) ---\r\n");
    sei(); 

    while (1) {
        
        // Commando check
        char cmd = UART_ReadChar_NonBlocking();
        if (cmd == 't') { temp_threshold = 2000; UART_TxString(">> CMD: 20.00C\r\n"); }
        else if (cmd == 'y') { temp_threshold = 3000; UART_TxString(">> CMD: 30.00C\r\n"); }
        else if (cmd == 'r') { temp_threshold = 2500; UART_TxString(">> CMD: Reset\r\n"); }

        if (timer_tick) {
            timer_tick = 0;
            timestamp_counter++; 

            // 1. Sensing (Nu ook Druk!)
            uint16_t light_raw = ADC_Read(1);      
            int32_t temp_raw = BMP280_ReadRawTemp();
            int32_t press_raw = BMP280_ReadRawPress(); // Nieuw

            // 2. Processing
            // BELANGRIJK: Eerst Temp berekenen, want die vult 't_fine' die nodig is voor Druk!
            int32_t temp_c_fine = BMP280_Compensate_T(temp_raw);
            uint32_t pressure_pa = BMP280_Compensate_P(press_raw); // Nieuw: Druk in Pascal
            
            uint16_t lumen = ((uint32_t)light_raw * MAX_LUMEN) / 1023;

            // Health Flag
            uint8_t status_flag = 1; 
            if (temp_c_fine < -4000 || temp_c_fine > 8500) status_flag = 0; 

            // Control Loop (Fan op Temp)
            uint8_t fan_speed = 0;
            if (temp_c_fine < temp_threshold) {
                fan_speed = 0;
            } else if (temp_c_fine >= (temp_threshold + TEMP_RANGE)) {
                fan_speed = FAN_MAX_PWM;
            } else {
                int32_t delta = temp_c_fine - temp_threshold;
                int32_t pwm_range = FAN_MAX_PWM - FAN_MIN_PWM;
                fan_speed = FAN_MIN_PWM + ((delta * pwm_range) / TEMP_RANGE);
            }

            // 3. Actuation
            PWM_SetDuty(fan_speed);

            // 4. Reporting (JSON Update met Pressure)
            UART_TxString("{");
            UART_TxString("\"ID\":");     UART_SendInt(NODE_ID);
            UART_TxString(",\"Time\":");  UART_SendLong(timestamp_counter);
            UART_TxString(",\"Stat\":");  UART_SendInt(status_flag); 
            UART_TxString(",\"Lum\":");   UART_SendInt(lumen);
            
            UART_TxString(",\"Temp\":");
            UART_SendInt(temp_c_fine / 100); UART_TxChar('.'); UART_SendInt(temp_c_fine % 100);
            
            UART_TxString(",\"Pres\":");  // Pressure in Pascal
            UART_SendLong(pressure_pa);
            
            UART_TxString(",\"Fan\":");   UART_SendInt(fan_speed);
            UART_TxString("}\r\n");

        } else {
            set_sleep_mode(SLEEP_MODE_IDLE);
            sleep_enable();
            sleep_cpu();
            sleep_disable();
        }
    }
    return 0;
}

// ---------------------------------------------------------
// DRIVERS 
// ---------------------------------------------------------

// UART, PWM, ADC, Timer, I2C Drivers blijven hetzelfde...
void UART_Init(void) { UBRR0H = 0; UBRR0L = 103; UCSR0B = (1 << TXEN0) | (1 << RXEN0); UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); }
char UART_ReadChar_NonBlocking(void) { if (UCSR0A & (1 << RXC0)) return UDR0; return 0; }
void UART_TxChar(char c) { while (!(UCSR0A & (1 << UDRE0))); UDR0 = c; }
void UART_TxString(const char* str) { while (*str) UART_TxChar(*str++); }
void UART_SendInt(uint16_t num) { char buffer[7]; int i = 0; if (num == 0) { UART_TxChar('0'); return; } while (num > 0) { buffer[i++] = (num % 10) + '0'; num /= 10; } while (i > 0) UART_TxChar(buffer[--i]); }
void UART_SendLong(uint32_t num) { char buffer[12]; int i = 0; if (num == 0) { UART_TxChar('0'); return; } while (num > 0) { buffer[i++] = (num % 10) + '0'; num /= 10; } while (i > 0) UART_TxChar(buffer[--i]); }
void ADC_Init(void) { ADMUX = (1 << REFS0); ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); }
uint16_t ADC_Read(uint8_t channel) { ADMUX = (ADMUX & 0xF8) | (channel & 0x07); ADCSRA |= (1 << ADSC); while (ADCSRA & (1 << ADSC)); return ADC; }
void PWM_Init(void) { DDRD |= (1 << PD5); TCCR0A = (1 << COM0B1) | (1 << WGM00) | (1 << WGM01); TCCR0B = (1 << CS01) | (1 << CS00); }
void PWM_SetDuty(uint8_t duty) { OCR0B = duty; }
void Timer1_Init(void) { OCR1A = 31250; TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10); TIMSK1 |= (1 << OCIE1A); }
ISR(TIMER1_COMPA_vect) { timer_tick = 1; }
void I2C_Init(void) { TWSR = 0x00; TWBR = 72; TWCR = (1 << TWEN); }
void I2C_Start(void) { TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN); while (!(TWCR & (1 << TWINT))); }
void I2C_Stop(void) { TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); }
void I2C_Write(uint8_t data) { TWDR = data; TWCR = (1 << TWINT) | (1 << TWEN); while (!(TWCR & (1 << TWINT))); }
uint8_t I2C_ReadACK(void) { TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA); while (!(TWCR & (1 << TWINT))); return TWDR; }
uint8_t I2C_ReadNACK(void) { TWCR = (1 << TWINT) | (1 << TWEN); while (!(TWCR & (1 << TWINT))); return TWDR; }

// --- BMP280 Helper Functions ---
uint8_t read8(uint8_t reg) { I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 0); I2C_Write(reg); I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 1); uint8_t res = I2C_ReadNACK(); I2C_Stop(); return res; }
uint16_t read16(uint8_t reg) { I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 0); I2C_Write(reg); I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 1); uint8_t lsb = I2C_ReadACK(); uint8_t msb = I2C_ReadNACK(); I2C_Stop(); return (msb << 8) | lsb; }
void BMP280_Init(void) { I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 0); I2C_Write(0xF4); I2C_Write(0x27); I2C_Stop(); }

// --- Uitgebreide Kalibratie Lezen (Temp + Druk) ---
void BMP280_ReadTrimming(void) {
    dig_T1 = read16(0x88);
    dig_T2 = (int16_t)read16(0x8A);
    dig_T3 = (int16_t)read16(0x8C);

    dig_P1 = read16(0x8E);
    dig_P2 = (int16_t)read16(0x90);
    dig_P3 = (int16_t)read16(0x92);
    dig_P4 = (int16_t)read16(0x94);
    dig_P5 = (int16_t)read16(0x96);
    dig_P6 = (int16_t)read16(0x98);
    dig_P7 = (int16_t)read16(0x9A);
    dig_P8 = (int16_t)read16(0x9C);
    dig_P9 = (int16_t)read16(0x9E);
}

// --- Ruwe Data Lezen ---
int32_t BMP280_ReadRawTemp(void) {
    I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 0); I2C_Write(0xFA); // Temp Registers
    I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 1);
    uint8_t msb = I2C_ReadACK(); uint8_t lsb = I2C_ReadACK(); uint8_t xlsb = I2C_ReadNACK(); I2C_Stop();
    return ((int32_t)msb << 12) | ((int32_t)lsb << 4) | (xlsb >> 4);
}

int32_t BMP280_ReadRawPress(void) {
    I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 0); I2C_Write(0xF7); // Press Registers
    I2C_Start(); I2C_Write((BMP280_ADDR << 1) | 1);
    uint8_t msb = I2C_ReadACK(); uint8_t lsb = I2C_ReadACK(); uint8_t xlsb = I2C_ReadNACK(); I2C_Stop();
    return ((int32_t)msb << 12) | ((int32_t)lsb << 4) | (xlsb >> 4);
}

// --- Math Magic (Bosch Formules) ---

// 1. Temperatuur (Update ook t_fine!)
int32_t BMP280_Compensate_T(int32_t adc_T) {
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2; // Update de globale t_fine
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

// 2. Druk (Heeft t_fine nodig van Temp functie)
uint32_t BMP280_Compensate_P(int32_t adc_P) {
    int64_t var1, var2, p; // 64-bit nodig voor precisie
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    
    if (var1 == 0) return 0; // Voorkom delen door 0
    
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    
    return (uint32_t)(p / 256); // Resultaat in Pascal (Pa)
}