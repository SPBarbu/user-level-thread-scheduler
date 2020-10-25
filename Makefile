CC=gcc
CFLAGS=-fsanitize=signed-integer-overflow -fsanitize=undefined -g -std=gnu99 -O0 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wshadow -Wno-unused-result

TEST=mytest

sut:
	$(CC) $(TEST).c sut.c -l pthread -o mytest $(CFLAGS)
clean:
	rm -rf mytest