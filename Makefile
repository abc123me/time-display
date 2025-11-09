OBJS=gpio.o
CFLAGS=-Wno-write-strings -Wno-pointer-arith
LIBS=-lpthread
CC=g++

all: main

clean_objs:
	rm -fv *.o
clean: clean_objs
	rm -fv bin/*

install:
	sudo cp bin/main /bin/time_display
	sudo cp time_display.service /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable time_display
	sudo systemctl start time_display
	sudo systemctl status time_display
uninstall:
	sudo systemctl stop time_display
	sudo systemctl disable time_display
	sudo rm /bin/time_display
	sudo rm /etc/systemd/system/time_display.service

%.o: %.cpp
	$(CC) $(LIBS) -c $< -o $@ $(CFLAGS)
main: $(OBJS)
	$(CC) $(LIBS) $(CFLAGS) main.cpp $(OBJS) -o bin/main
