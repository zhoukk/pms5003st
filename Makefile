all: pms5003st pms5003st_mqtt

pms5003st: uart_usb_pms5003st.c
	gcc -O3 -g -Wall -Wextra -pedantic -o $@ $<

pms5003st_mqtt: pms5003st_mqtt.c ../libmqtt/lib/ae.c ../libmqtt/lib/anet.c ../libmqtt/lib/zmalloc.c ../libmqtt/.libs/libmqtt.a
	gcc -O3 -g -Wall -Wextra -pedantic -I../libmqtt -o $@ $^

clean:
	-rm pms5003st
	-rm pms5003st_mqtt