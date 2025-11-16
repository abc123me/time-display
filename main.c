#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include "time.h"
#include "signal.h"
#include "unistd.h"
#include "gpiod.h"
#include "pthread.h"
#include "sys/time.h"
#include "sys/types.h"
#include "sys/resource.h"

#define DISPLAY_LED_PIN 7 // CON2-P29 --> PA7

void set_digits(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
void set_rtpriority();
int init_chip();
int init_gpio();
void free_gpio();
void interrupt(int);

struct gpio_ref {
	struct gpiod_chip *chip;
	struct gpiod_line_config *cfg;
	struct gpiod_line_settings *setts;
	struct gpiod_request_config *rcfg;
	struct gpiod_line_request *rqst;
};

volatile uint8_t run;
FILE *chipFD = NULL;

int main(int argc, char** argv) {
	struct gpio_ref ref;
	char *gpio_fname = "/dev/gpiochip0";
	char *pid_fname = "/var/run/time-display.pid";
	FILE *pid_fp = NULL;
	int ret = 0;

	/* Initialize the shift register */
	ret = init_chip();
	if(ret) goto gtfo_chip;

	/* Initialize the colon LED output */
	ret = init_gpio(&ref, gpio_fname, DISPLAY_LED_PIN);
	if(ret) goto gtfo_gpio;

	/* Create a PID file if specified */
	if(pid_fname) {
		pid_fp = fopen(pid_fname, "w");
		if(pid_fp) {
			fprintf(pid_fp, "%d", getpid());
			fclose(pid_fp);
			printf("PID file created at %s\n", pid_fname);
		} else {
			ret = 1;
			fprintf(stderr, "Failed to make PID file at %s!\n", pid_fname);
			goto gtfo_pidf;
		}
	}

	/* Set the RT priority */
	set_rtpriority();

	/* Enable the main loop */
	run = 1;

	/* Start the main loop */
	time_t rawTime;
	struct tm* tInfo;
	uint8_t cntr = 0;
	uint8_t ledState = 0;
	uint8_t lastSeconds = 0;
	while(run) {
		time(&rawTime);
		tInfo = localtime(&rawTime);
		uint8_t hours = tInfo->tm_hour;
		uint8_t minutes = tInfo->tm_min;
		uint8_t seconds = tInfo->tm_sec;
		set_digits(seconds % 10, seconds / 10,
			minutes % 10, minutes / 10,
			hours % 10, hours / 10);

		if(lastSeconds != seconds) {
			gpiod_line_request_set_value(ref.rqst, DISPLAY_LED_PIN, GPIOD_LINE_VALUE_ACTIVE);
			cntr = 0;
		}
		if(cntr > 1) {
			gpiod_line_request_set_value(ref.rqst, DISPLAY_LED_PIN, GPIOD_LINE_VALUE_INACTIVE);
			cntr = 0;
		} else cntr++;

		usleep(250000);
		lastSeconds = seconds;
	}

	/* Clear the display prior to exit */
	set_digits(0, 0, 0, 0, 0, 0);
	gpiod_line_request_set_value(ref.rqst, DISPLAY_LED_PIN, GPIOD_LINE_VALUE_INACTIVE);

	/* Delete the PID file if one exists */
	if(pid_fname && pid_fp && remove(pid_fname) != 0)
		fprintf(stderr, "Warning - Failed to remove PID file (%s)\n", pid_fname);

	/* Cleanup and exit */
gtfo_pidf:
	puts("Closing LED GPIO info!");
	free_gpio(&ref);
gtfo_gpio:
	puts("Closing 74HC595 driver!");
	fclose(chipFD);
gtfo_chip:
	return ret;
}
void interrupt(int sig) {
	run = 0;
}

uint8_t bit_at(uint8_t dat, uint8_t pos) { return dat & pos ? 1 : 0; }
void set_digits(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6) {
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
void set_rtpriority() {
	struct sched_param params;

	puts("Setting RT priority and other process options");
	params.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &params) != 0)
		puts("Failed to set realtime priority!");
	puts("Attaching interrupt handler, Use Ctrl+C to stop!");
	signal(SIGINT, interrupt);
}
int init_chip() {
	puts("Opening shift registers at /dev/chip74hc595");
	chipFD = fopen("/dev/chip74hc595", "wb");
	if(!chipFD) {
		fprintf(stderr, "Failed to open shift registers!\n");
		return 1;
	}
	setbuf(chipFD, NULL);
	return 0;
}
int init_gpio(struct gpio_ref *ref, char* chip, unsigned int offset) {
	memset(ref, 0, sizeof(struct gpio_ref));
	/* Create the settings object */
	ref->setts = gpiod_line_settings_new();
	if(ref->setts == NULL) {
		fprintf(stderr, "Failed to initialize GPIO line settings for pin %u\n", offset);
		goto gtfo;
	}
	gpiod_line_settings_set_direction(ref->setts, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_drive(ref->setts, GPIOD_LINE_DRIVE_PUSH_PULL);
	gpiod_line_settings_set_output_value(ref->setts, GPIOD_LINE_VALUE_INACTIVE);
	/* Create the config object for the settings */
	ref->cfg = gpiod_line_config_new();
	if(ref->cfg == NULL) {
		fprintf(stderr, "Failed to initialize GPIO line config for pin %u\n", offset);
		goto gtfo;
	}
	gpiod_line_config_add_line_settings(ref->cfg, &offset, 1, ref->setts);
	/* Create the request config for the settings */
	ref->rcfg = gpiod_request_config_new();
	if(ref->rcfg == NULL) {
		fprintf(stderr, "Failed to initialize GPIO request config\n");
		goto gtfo;
	}
	gpiod_request_config_set_consumer(ref->rcfg, "time-display");
	/* Open the GPIO chip */
	ref->chip = gpiod_chip_open(chip);
	if(ref->chip == NULL) {
		fprintf(stderr, "Failed to open GPIO chip (%s)\n", chip);
		goto gtfo;
	}
	/* Request the line from the chip */
	ref->rqst = gpiod_chip_request_lines(ref->chip, ref->rcfg, ref->cfg);
	if(ref->rqst == NULL) {
		fprintf(stderr, "Failed to request line from GPIO chip (%s) for pin %u\n", chip, offset);
		goto gtfo;
	}
	/* PROFIT!!! */
	return 0;

	/* Handle errors */
gtfo:
	free_gpio(ref);
	return 1;
}
void free_gpio(struct gpio_ref *ref) {
	if(ref->rqst)
		gpiod_line_request_release(ref->rqst);
	ref->rqst = NULL;

	if(ref->chip)
		gpiod_chip_close(ref->chip);
	ref->chip = NULL;

	if(ref->rcfg)
		gpiod_request_config_free(ref->rcfg);
	ref->rcfg = NULL;

	if(ref->cfg)
		gpiod_line_config_free(ref->cfg);
	ref->cfg = NULL;

	if(ref->setts)
		gpiod_line_settings_free(ref->setts);
	ref->setts = NULL;
}
