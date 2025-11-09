#include "gpio.h"

#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"
#include "cstring"

#define GPIO_STATE_INIT		0
#define GPIO_STATE_EXPORTED	1
#define GPIO_STATE_IS_OUTPUT	2

typedef uint8_t gpio_err_t;

void gpio_t::free_strs() {
	if(val_fn) { free(val_fn); val_fn = NULL; }
	if(dir_fn) { free(dir_fn); dir_fn = NULL; }
}
gpio_t::gpio_t(uint32_t pin) : pin(pin), val_fn(NULL), dir_fn(NULL), state(GPIO_STATE_INIT) {}
gpio_t::~gpio_t() { free_strs(); }

gpio_err_t gpio_t::begin() {
	if(state & GPIO_STATE_EXPORTED)
		return GPIO_ALREADY_INITIALIZED;
	FILE* fp = fopen("/sys/class/gpio/export", "w");
	if(!fp) return GPIO_FAILED_TO_OPEN;
	gpio_err_t err = GPIO_SUCCESS;
	if(fprintf(fp, "%u\n", pin) <= 1)
		err = GPIO_FAILED_TO_WRITE;
	fclose(fp);
	if(!err) {
		state |= GPIO_STATE_EXPORTED;
		free_strs();
		val_fn = (char*) malloc(128);
		dir_fn = (char*) malloc(128);
		snprintf(val_fn, 128, "/sys/class/gpio/gpio%u/value", pin);
		snprintf(dir_fn, 128, "/sys/class/gpio/gpio%u/direction", pin);
	}
	return err;
}
gpio_err_t write_and_close(char* fn, char* txt) {
	FILE* fp = fopen(fn, "w");
	if(!fp) return GPIO_FAILED_TO_OPEN;
	gpio_err_t err = GPIO_SUCCESS;
	uint8_t len = strlen(txt);
	if(fwrite(txt, sizeof(char), len, fp) != len) err = GPIO_FAILED_TO_WRITE;
	fclose(fp);
	return err;
}
gpio_err_t gpio_t::set_direction(uint8_t out) {
	if(!dir_fn) return GPIO_NOT_YET_INITIALIZED;
	gpio_err_t err = write_and_close(dir_fn, (char*) (out ? "out\n" : "in\n"));
	if(out && !err) state |= GPIO_STATE_IS_OUTPUT;
	return err;
}
gpio_err_t gpio_t::write(uint8_t val) {
	if(!val_fn) return GPIO_NOT_YET_INITIALIZED;
	if(state & GPIO_STATE_IS_OUTPUT)
		return write_and_close(val_fn, (char*) (val ? "1\n" : "0\n"));
	else
		return GPIO_PIN_NOT_SET_AS_OUTPUT;
}
gpio_err_t gpio_t::read() {
	return GPIO_NOT_YET_IMPLEMENTED;
}
void gpio_t::end() {
	state &= ~GPIO_STATE_EXPORTED;
	free_strs();
	FILE* fp = fopen("/sys/class/gpio/unexport", "w");
	if(!fp) return;
	fprintf(fp, "%u\n", pin);
	fclose(fp);
}

char* gpio_err_tostr(gpio_err_t v) {
	if(v == 0) return NULL;
	switch(v) {
		case GPIO_NOT_YET_INITIALIZED:		return "GPIO pin is not initialized";
		case GPIO_NOT_YET_IMPLEMENTED:		return "Not yet implemented";
		case GPIO_ALREADY_INITIALIZED:		return "GPIO pin already initialized";
		case GPIO_PIN_NOT_SET_AS_OUTPUT:	return "GPIO pin is not an output";
		case GPIO_FAILED_TO_WRITE:		return "Failed to write to file";
		case GPIO_FAILED_TO_OPEN:		return "Failed to open file";
		default:				return "Unknown error";
	}
}

