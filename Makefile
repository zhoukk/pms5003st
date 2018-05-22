all: pms5003st pms5003st_mqtt

pms5003st: pms5003st.c
	gcc -O3 -g -Wall -Wextra -o $@ $<

pms5003st_mqtt: pms5003st_mqtt.c
	gcc -O3 -g -Wall -Wextra -I../libmqtt -o $@ $^

clean:
	-rm pms5003st
	-rm pms5003st_mqtt