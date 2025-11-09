#include "stdint.h"
#include "stdlib.h"
#include "stdio.h"
#include "time.h"
#include "signal.h"
#include "unistd.h"
#include "gpiod.h"
#include "pthread.h"
#include "sys/time.h"
#include "sys/resource.h"

#define DISPLAY_LED_PIN 7 // CON2-P29 --> PA7

void setDigits(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
void setPriority();
int8_t initChip();
void interrupt(int);

FILE* chipFD = NULL;
volatile uint8_t runDisplay = 1;

int main(int argc, char** argv) {
	int ret;
	/* Initialize the shift register */
	ret = initChip();
	if(ret != 0) goto gtfo_chip;

	/* Initialize the colon LED output */
	enum gpiod_line_value val;

	struct gpiod_chip *chip;

	struct gpiod_line_settings *setts;
	setts = gpiod_line_settings_new();
	gpiod_line_settings_set_direction(setts, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_drive(setts, GPIOD_LINE_DRIVE_PUSH_PULL);
	gpiod_line_settings_set_output_value(setts, GPIOD_LINE_VALUE_INACTIVE);

	struct gpiod_line_config *cfg;
	unsigned int offset = 0;
	cfg = gpiod_line_config_new();
	gpiod_line_config_add_line_settings(cfg, &offset, 1, setts);

	struct gpiod_request_config *rcfg;
	rcfg = gpiod_request_config_new();
	gpiod_request_config_set_consumer(rcfg, "time-display");

	struct gpiod_line_request *rqst;
	rqst = gpiod_chip_request_lines(chip, rcfg, cfg);

	/* Set the RT priority */
	setPriority();

	time_t rawTime;
	struct tm* tInfo;
	uint8_t cntr = 0;
	uint8_t ledState = 0;
	uint8_t lastSeconds = 0;
	while(runDisplay) {
		time(&rawTime);
		tInfo = localtime(&rawTime);
		uint8_t hours = tInfo->tm_hour;
		uint8_t minutes = tInfo->tm_min;
		uint8_t seconds = tInfo->tm_sec;
		setDigits(seconds % 10, seconds / 10,
			minutes % 10, minutes / 10,
			hours % 10, hours / 10);

		if(lastSeconds != seconds) {
			gpiod_line_request_set_value(rqst, 0, GPIOD_LINE_VALUE_ACTIVE);
			cntr = 0;
		}
		if(cntr > 1) {
			gpiod_line_request_set_value(rqst, 0, GPIOD_LINE_VALUE_INACTIVE);
			cntr = 0;
		} else cntr++;

		usleep(250000);
		lastSeconds = seconds;
	}

	setDigits(0, 0, 0, 0, 0, 0);
	gpiod_line_request_set_value(rqst, 0, GPIOD_LINE_VALUE_INACTIVE);

	puts("Closing LED GPIO info!");
	gpiod_line_request_release(rqst);
	gpiod_line_config_free(cfg);
	gpiod_line_settings_free(setts);
	gpiod_request_config_free(rcfg);
	puts("Closing 74HC595 driver!");
gtfo_led:
	fclose(chipFD);
gtfo_chip:
	return ret;
}
void interrupt(int sig) {
	runDisplay = 0;
}


uint8_t bit_at(uint8_t dat, uint8_t pos) { return dat & pos ? 1 : 0; }
void setDigits(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6) {
	uint8_t buf[3] = {0, 0, 0};
	// Digit 1, values 0 through F (lower seconds)
	buf[2] |= bit_at(d1, 1) << 5; // Digit 1, bit 1
	buf[2] |= bit_at(d1, 2) << 6; // Digit 1, bit 2
	buf[1] |= bit_at(d1, 4) << 6; // Digit 1, bit 3
	buf[2] |= bit_at(d1, 8) << 1; // Digit 1, bit 4
	// Digit 2, values 0 through 7 (higher seconds)
	// Digit 2 bit 4 is always zero
	buf[2] |= bit_at(d2, 1) << 2; // Digit 2, bit 1
	buf[1] |= bit_at(d2, 2) << 7; // Digit 2, bit 2
	buf[1] |= bit_at(d2, 4) << 5; // Digit 2, bit 3
	// Digit 3, values 0 through F (lower minutes)
	buf[2] |= bit_at(d3, 1) << 4; // Digit 3, bit 1
	buf[0] |= bit_at(d3, 2) << 4; // Digit 3, bit 2
	buf[0] |= bit_at(d3, 4) << 5; // Digit 3, bit 3
	buf[0] |= bit_at(d3, 8) << 6; // Digit 3, bit 4
	// Digit 4, values 0 through 7 (higher minutes)
	// Digit 4 bit 4 is always zero
	buf[0] |= bit_at(d4, 1) << 2; // Digit 4, bit 1
	buf[0] |= bit_at(d4, 2) << 1; // Digit 4, bit 2
	buf[0] |= bit_at(d4, 4) << 3; // Digit 4, bit 3
	// Digit 5, values 0 through F (lower hours)
	buf[2] |= bit_at(d5, 1) << 3; // Digit 5, bit 1
	buf[1] |= bit_at(d5, 2) << 2; // Digit 5, bit 2
	buf[1] |= bit_at(d5, 4) << 1; // Digit 5, bit 3
	buf[0] |= bit_at(d5, 8) << 7; // Digit 5, bit 4
	// Digit 6, values 0 through 3 (higher hours)
	// Digit 6 bits 3 & 4 are always zero
	buf[1] |= bit_at(d6, 1) << 3; // Digit 6, bit 1
	buf[1] |= bit_at(d6, 2) << 4; // Digit 6, bit 2
	// Invert the partial-BCD segment values
	// (this is because the darlington arrays invert the segment values)
	buf[0] = ~buf[0]; buf[1] = ~buf[1]; buf[2] = ~buf[2];
	// Write the buffer's data
	fwrite(buf, 3, 1, chipFD);
}
void setPriority() {
	struct sched_param params;

	puts("Setting RT priority and other process options");
	params.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &params) != 0)
		puts("Failed to set realtime priority!");
	puts("Attaching interrupt handler, Use Ctrl+C to stop!");
	signal(SIGINT, interrupt);
}
int8_t initChip() {
	puts("Opening shift registers at /dev/74HC595");
	chipFD = fopen("/dev/chip74hc595", "wb");
	if(!chipFD) {
		puts("Failed to open shift registers!");
		return 1;
	}
	setbuf(chipFD, NULL);
	return 0;
}
int8_t initLED() {
	printf("Opening GPIO%d as LED output\n", DISPLAY_LED_PIN);
	gpio_err_t err;
	err = led.begin();
	if(err != GPIO_SUCCESS) {
		printf("Failed to open GPIO pin: %s\n", gpio_err_tostr(err));
		return 1;
	}
	err = led.set_direction(GPIO_DIRECTION_OUTPUT);
	if(err != GPIO_SUCCESS) {
		printf("Failed to set GPIO as output: %s\n", gpio_err_tostr(err));
		led.end();
		return 1;
	}
	return 0;
}
