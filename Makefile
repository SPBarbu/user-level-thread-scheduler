CC=gcc
CFLAGS=-fsanitize=signed-integer-overflow -fsanitize=undefined -g -std=gnu99 -O0 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wshadow -Wno-unused-result

sut:
	$(CC) -c sut.c -o sut.o -l pthread $(CFLAGS)
clean:
	rm -rf ults