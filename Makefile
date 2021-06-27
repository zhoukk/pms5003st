all: pms5003st_print pms5003st_pub pms5003st_sub

pms5003st_print: pms5003st_print.c
	gcc -O3 -g -Wall -Wextra -o $@ $<

pms5003st_pub: pms5003st_pub.c
	gcc -O3 -g -Wall -Wextra -lpthread -o $@ $^

pms5003st_sub: pms5003st_sub.c http_parser.c
	gcc -O3 -g -Wall -Wextra -o $@ $^

clean:
	-rm pms5003st_print
	-rm pms5003st_pub
	-rm pms5003st_sub