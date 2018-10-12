/*
 * main.c
 *
 *  Created on: 31.05.2018
 *      Author: max
 */

#include <avr/io.h>
#include <util/delay.h>
#include <compat/twi.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>

//reset
#define RESET_BIT			PC6
#define RESET_DDR			DDRC
#define RESET_PORT			PORTC

//���������
#define LED_BIT				PB2
#define LED_DDR				DDRB
#define LED_PORT			PORTB

//�������
#define BUZZER_BIT			PD3
#define BUZZER_DDR			DDRD
#define BUZZER_PORT			PORTD

//������
#define MOTOR0_PWM_BIT		PD6
#define MOTOR0_PWM_DDR		DDRD
#define MOTOR0_PWM_PORT		PORTD

#define MOTOR0_INA_BIT		PB1
#define MOTOR0_INA_DDR		DDRB
#define MOTOR0_INA_PORT		PORTB

#define MOTOR0_INB_BIT		PD7
#define MOTOR0_INB_DDR		DDRD
#define MOTOR0_INB_PORT		PORTD

#define MOTOR0_TIMER_REG	OCR0A

#define MOTOR1_PWM_BIT		PD5
#define MOTOR1_PWM_DDR		DDRD
#define MOTOR1_PWM_PORT		PORTD

#define MOTOR1_INA_BIT		PD4
#define MOTOR1_INA_DDR		DDRD
#define MOTOR1_INA_PORT		PORTD

#define MOTOR1_INB_BIT		PD2
#define MOTOR1_INB_DDR		DDRD
#define MOTOR1_INB_PORT		PORTD

#define MOTOR1_TIMER_REG	OCR0B

//i2c
#define I2C_SLAVE_ADDR      0x27

#define REG_WHY_IAM			0x00
#define REG_ONLINE			0x01
#define REG_SERVO0			0x02
#define REG_SERVO1			0x03
#define REG_SERVO2			0x04
#define REG_SERVO3			0x05
#define REG_DIR0			0x06
#define REG_PWM0			0x07
#define REG_DIR1			0x08
#define REG_PWM1			0x09
#define REG_BEEP			0x0A


//MultiServo
#define MaxServo 		4 //���������� ����
#define Tick_1ms 		125 //���������� ����� ������� � 1��
#define	SERVO_PORT 		PORTC
#define	SERVO_DDR 		DDRC

uint8_t onLine = 0; //������� ���� ��� ������� �������� � Linux ����������
uint8_t offLineCount = 0;
uint8_t offLineLedCount = 0;

unsigned char regAddr; // Store the Requested Register Address
unsigned char regData; // Store the Register Address Data

uint8_t dirMotor0;
uint8_t dirMotor1;

typedef struct {
	uint8_t position;
	uint8_t bit;
} SArray_def;

SArray_def servo[MaxServo];
SArray_def *servoSorted[MaxServo];

//uint8_t servo_need_update = 0;
uint8_t servoState = 0; //�������� ��������� ��������
uint8_t servoPortState[MaxServo+1]; // �������� ����� ������� ���� �������
uint8_t servoNextOCR[MaxServo+1]; // ����� ������ ��������

uint8_t servoNeedUpdate;

uint8_t adcCounter = 0; //������� ��� �� ������� adcValueList
uint16_t tmpAdcSumm = 0; //����� �������� ��� ��� ����������
uint8_t averageCounter = 0; //������� ����������� ��������

uint8_t buzzerActive = 0;
uint8_t buzzerTime = 0;

void I2CSlaveAction(unsigned char rwStatus);
void ServoSort(void);
void ServoUpd(void);
void ServoInit(void);
void ServoSetPos(uint8_t servoNum, uint8_t pos);

ISR(TWI_vect)
{
    static unsigned char i2c_state;
    unsigned char twi_status;

    // Disable Global Interrupt
    cli();

    // Get TWI Status Register, mask the prescaler bits (TWPS1,TWPS0)
    twi_status=TWSR & 0xF8;

    switch(twi_status) {
        case TW_SR_SLA_ACK: // 0x60: SLA+W received, ACK returned
            i2c_state=0;    // Start I2C State for Register Address required
            break;

        case TW_SR_DATA_ACK:    // 0x80: data received, ACK returned
            if (i2c_state == 0) {
                regAddr = TWDR; // Save data to the register address
                i2c_state = 1;
            } else {
                regData = TWDR; // Save to the register data
                i2c_state = 2;
            }
            break;

        case TW_SR_STOP:    // 0xA0: stop or repeated start condition received while selected
            if (i2c_state == 2) {
            	I2CSlaveAction(1);    // Call Write I2C Action (rw_status = 1)
                i2c_state = 0;      // Reset I2C State
            }
            break;

        case TW_ST_SLA_ACK: // 0xA8: SLA+R received, ACK returned
        case TW_ST_DATA_ACK:    // 0xB8: data transmitted, ACK received
            if (i2c_state == 1) {
            	I2CSlaveAction(0);    // Call Read I2C Action (rw_status = 0)
                TWDR = regData;     // Store data in TWDR register
                i2c_state = 0;      // Reset I2C State
            }
            break;

        case TW_ST_DATA_NACK:   // 0xC0: data transmitted, NACK received
        case TW_ST_LAST_DATA:   // 0xC8: last data byte transmitted, ACK received
        case TW_BUS_ERROR:  // 0x00: illegal start or stop condition
        default:
            i2c_state = 0;  // Back to the Begining State
    }

    // Clear TWINT Flag
    TWCR |= (1<<TWINT);
    // Enable Global Interrupt
    sei();
}

ISR (TIMER1_COMPA_vect)
{ // ���������� �� ����������
	//cli(); //������ ����������

	uint8_t LenghtPPM = servoNextOCR[servoState]; 	// ����� ��������� ��������

	if (servoState)		// ���� �� ������� ���������
	{
		SERVO_PORT &= ~servoPortState[servoState];	// ���������� ���� � �����, � ������������ � ������ � ������� �����.

		if (LenghtPPM == 0xFF) // ���� �������� ��������� ����� FF ������ ��� ��������
		{					// � �� �������� ����� �������. � ���� �������� �������
			servoState = 0;	// ���������� ������� ��������� ��������.

			OCR1A = 20*Tick_1ms; 		//������ �������� �� 15��

			if (servoNeedUpdate)		// ���� �������� ������ �������� ������� ��������
			{
					ServoUpd();			// ��������� �������.
					servoNeedUpdate = 0;	// ���������� ������ ����������.
			}
		}
		else
		{
			OCR1A = Tick_1ms + LenghtPPM;	// � ������� ��������� ������ ��������� �������� (1�� + ��������)
			servoState++;					// ����������� ��������� ��������
		}
	}
	else
	{
		TCNT1 = 0;		//����� �������

		OCR1A = Tick_1ms + LenghtPPM;	// � ������� ��������� ������ ��������� �������� (1�� + ��������)
		SERVO_PORT |= 0b00001111;		// ���������� ��� ����������� � 1 - ������ ��������

		servoState++;					// ����������� ��������� ��������
	}

	//sei(); //���������� ����������
}

void I2CSlaveAction(unsigned char rwStatus)
{
	switch(regAddr) {

	// PORT
	case REG_WHY_IAM:
		if (rwStatus == 0)
			// read
			regData = 0x2A; //����� �� ������� ������ �����, ��������� � ����� ������
		break;
	case REG_ONLINE:
		if (rwStatus)
		{
			if (onLine == 0) //���� ������� � ��������
				onLine = 1;
			offLineCount = 0; //���������� ������� �������
		}
		break;
	case REG_SERVO0:
		if (rwStatus)
		{
			if (onLine)
				ServoSetPos(0, regData);
		}
		else
			regData = servo[0].position;
		break;
	case REG_SERVO1:
		if (rwStatus)
		{
			if (onLine)
				ServoSetPos(1, regData);
		}
		else
			regData = servo[1].position;
		break;
	case REG_SERVO2:
		if (rwStatus)
		{
			if (onLine)
				ServoSetPos(2, regData);
		}
		else
			regData = servo[2].position;
		break;
	case REG_SERVO3:
		if (rwStatus)
		{
			if (onLine)
				ServoSetPos(3, regData);
		}
		else
			regData = servo[3].position;
		break;
	case REG_DIR0:
		if (rwStatus)
		{
			if (onLine)
			{
				if (regData)
				{
					MOTOR0_INA_PORT |= (1 << MOTOR0_INA_BIT);
					MOTOR0_INB_PORT &= ~(1 << MOTOR0_INB_BIT);
					dirMotor0 = 1;
				}
				else
				{
					MOTOR0_INA_PORT &= ~(1 << MOTOR0_INA_BIT);
					MOTOR0_INB_PORT |= (1 << MOTOR0_INB_BIT);
					dirMotor0 = 0;
				}
			}
		}
		else
			regData = dirMotor0;
		break;
	case REG_PWM0:
		if (rwStatus)
		{
			if (onLine)
				MOTOR0_TIMER_REG = regData;
		}
		else
			regData = MOTOR0_TIMER_REG;
		break;
	case REG_DIR1:
		if (rwStatus)
		{
			if (onLine)
			{
				if (regData)
				{
					MOTOR1_INA_PORT |= (1 << MOTOR1_INA_BIT);
					MOTOR1_INB_PORT &= ~(1 << MOTOR1_INB_BIT);
					dirMotor1 = 1;
				}
				else
				{
					MOTOR1_INA_PORT &= ~(1 << MOTOR1_INA_BIT);
					MOTOR1_INB_PORT |= (1 << MOTOR1_INB_BIT);
					dirMotor1 = 0;
				}
			}
		}
		else
			regData = dirMotor1;
		break;
	case REG_PWM1:
		if (rwStatus)
		{
			if (onLine)
				MOTOR1_TIMER_REG = regData;
		}
		else
			regData = MOTOR1_TIMER_REG;
		break;
	case REG_BEEP:
		if (rwStatus)
		{
			buzzerTime = regData;
			TCCR2A |= (1 << COM2B0); //�������� ���� ��������
		}
		break;
	default:
		regData = 0x00;
	}
}

//���������� �������� ���������� ��������. �������� ���� �� ��������, �� �� ����� ����� �����������
// ������ ������� �������� �����������.
void ServoSort(void)
{
	uint8_t i, k;
	SArray_def *tmp;

	// ��������� ������ ����������.
	for(i=1; i<MaxServo; i++) {
		for(k=i; ((k>0)&&(servoSorted[k]->position < servoSorted[k-1]->position)); k--) {
			tmp = servoSorted[k];					// Swap [k,k-1]
			servoSorted[k] = servoSorted[k-1];
			servoSorted[k-1] = tmp;
		}
	}
}

void ServoUpd(void)
{
	uint8_t i,j,k;

	for(i=0, k=0; i<MaxServo; i++, k++)
	{
		if(servoSorted[i]->position != servoSorted[i+1]->position)	//���� �������� ����������
		{
			servoNextOCR[k] = servoSorted[i]->position;			// ���������� �� ��� ����
			servoPortState[k+1] = servoSorted[i]->bit;			// � �������� ���� ��
		}
		else								// �� ���� ��������� �� ���������
		{
			servoNextOCR[k] = servoSorted[i]->position;			// ������� ����������
			servoPortState[k+1] = servoSorted[i]->bit;			// ���������� ��������

			// � � ����� ���� ��� ����������� �������, �������� �� �������� � ����.

			for(j=1; (servoSorted[i]->position == servoSorted[i+j]->position)&&(i+j < MaxServo); j++)
				servoPortState[k+1] |= servoSorted[i+j]->bit;
			i+=j-1;						// ����� ������� ������������ ������
		}						// �� ������� ��������� � �������
	}
	servoNextOCR[k] = 0xFF;		// � ��������� ������� ��������� �������� FF.
}

void ServoInit(void) {
	servoSorted[0] = &servo[0];
	servoSorted[1] = &servo[1];
	servoSorted[2] = &servo[2];
	servoSorted[3] = &servo[3];


	servo[0].bit = 0b00000001;
	servo[1].bit = 0b00000010;
	servo[2].bit = 0b00000100;
	servo[3].bit = 0b00001000;

	//ServoSetZeroPos(NULL);

	//ServoSort();
	ServoUpd(); //�������� ��� ���������� �������� servoNextOCR � servoPortState

	//��������� ������� 1 ��� ������������ PPM ��������
	TCCR1A = 0; //������� ����� ������ �������
	TCCR1B = (3 << CS10); //������������ �� 64, 8000000/64/1000 = 1�� = 125 ����� �������
	TIMSK1 = (1 << OCIE1A); //���������� ������� �� ���������� � OCR1A
	TCNT1 = 0;		//����� �������
	OCR1A = 0;

	//printf_P(PSTR("Servo: %u\n"), MaxServo);
}

void ServoSetPos(uint8_t servoNum, uint8_t pos) { //������ ��������� ������������
	if (pos > Tick_1ms)
		servo[servoNum].position = Tick_1ms;
	else
		servo[servoNum].position = pos; //������ �������
	ServoSort();
	servoNeedUpdate = 1;
}

void SetAllServoMiddlePos(void) { //���������� ��� ������������ � ������� ���������
	for ( uint8_t i = 0; i < MaxServo ; i++ )
	{
		servo[i].position = Tick_1ms >> 1;
	}
	ServoSort();
	servoNeedUpdate = 1;
}

void OffLineAction(void)
{
	//���� ������
	MOTOR0_TIMER_REG = 0;
	MOTOR1_TIMER_REG = 0;

	SetAllServoMiddlePos(); //��� ������������ � ������� ���������
}


int main(void)
{
	// �������� TWI Pull UP
	PORTC |= (1 << PC4) | (1 << PC5);

	LED_DDR |= (1<<LED_BIT); //�������������� ���������

	BUZZER_DDR |= (1<<BUZZER_BIT); //������� 2400 ��, ���� �� �����

	//������ 2, ����� CTC, ��������� �����
	TCCR2A = (0 << COM2B0) | (1 << WGM21);
	//������������ 8 8000000/8 = 1000000��
	TCCR2B = (2 << CS20);
	TCNT2 = 0;
	OCR2A = 208; //4800�� /2 =2400�� ������� �������


	//������
	MOTOR0_PWM_DDR |= (1 << MOTOR0_PWM_BIT);
	MOTOR0_INA_DDR |= (1 << MOTOR0_INA_BIT);
	MOTOR0_INB_DDR |= (1 << MOTOR0_INB_BIT);

	MOTOR1_PWM_DDR |= (1 << MOTOR1_PWM_BIT);
	MOTOR1_INA_DDR |= (1 << MOTOR1_INA_BIT);
	MOTOR1_INB_DDR |= (1 << MOTOR1_INB_BIT);

	//������ 0, ��� �� ������ FastPWM
	TCCR0A = (1 << COM0A1) | (1 << COM0B1) | (1 << WGM01) | (1 << WGM00);
	//������������ 64 8000000/256/8 = 3906��
	TCCR0B = (2 << CS00);

	//�����
	SERVO_DDR |= 0b00001111;	// ��� ������ ����� ��������� ��� �����
	ServoInit(); //������������� �������� � ���������� ��� ������������ �������� ���������� ��������������
	SetAllServoMiddlePos(); //��� ������������ � ������� ���������

	// Initial I2C Slave
	TWAR = (I2C_SLAVE_ADDR << 1) & 0xFE;    // Set I2C Address, Ignore I2C General Address 0x00
	TWDR = 0x00;            // Default Initial Value

	// Start Slave Listening: Clear TWINT Flag, Enable ACK, Enable TWI, TWI Interrupt Enable
	TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN) | (1<<TWIE);

	// Initial Variable Used
	regAddr = 0;
	regData = 0;

	buzzerTime = 3; //�������� �� ������
	TCCR2A |= (1 << COM2B0); //�������� ���� ��������

	sei();

	while(1)
	{
		offLineCount++; //����������� ������� ��������
		if (offLineCount > 30) //���� �������� �� 30 (3 �������), ������ ������� �������� ������ "����������"
		{
			offLineCount = 0;
			if (onLine)
			{
				onLine = 0; //������� � ��������
				OffLineAction(); //��������� �������� ��� �������� �������
			}

		}

		if (onLine)
			LED_PORT ^= (1<<LED_BIT); //����������� ���������
		else
		{
			offLineLedCount++; //������� ��������� ��� ������ �������, ����� ��������� ����� ���������
			if (offLineLedCount > 10)
			{
				offLineLedCount = 0;
				LED_PORT ^= (1<<LED_BIT); //����������� ���������
			}
		}

		if (buzzerTime)
			buzzerTime--;
		else
			TCCR2A &= ~(1 << COM2B0); //��������� ���� ��������


		_delay_ms(100);
	}
}
