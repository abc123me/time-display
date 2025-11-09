#ifndef _GPIO_H
#define _GPIO_H

#include "stdint.h"

#define GPIO_DIRECTION_INPUT		0
#define GPIO_DIRECTION_OUTPUT		1

#define GPIO_SUCCESS			0
#define GPIO_FAILED_TO_OPEN		2
#define GPIO_FAILED_TO_WRITE		3
#define GPIO_PIN_NOT_SET_AS_OUTPUT  	4
#define GPIO_ALREADY_INITIALIZED	5
#define GPIO_NOT_YET_INITIALIZED	6
#define GPIO_NOT_YET_IMPLEMENTED	7
typedef uint8_t gpio_err_t;

char* gpio_err_tostr(gpio_err_t v);

class gpio_t {
private:
	uint32_t pin;
	uint8_t state;
	char* val_fn;
	char* dir_fn;

	void free_strs();
public:
	gpio_t(uint32_t pin);
	~gpio_t();
	gpio_err_t begin();
	gpio_err_t set_direction(uint8_t out);
	gpio_err_t write(uint8_t val);
	gpio_err_t read();
	void end();
};

#endif
