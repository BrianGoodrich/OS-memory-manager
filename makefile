CC = gcc -g3
CFLAGS = -g3

all: user oss

user: user.o
	$(CC) -o user user.o

oss: oss.o
	$(CC) -o oss oss.o

user.o: user.c
	$(CC) $(CFLAGS) -c user.c

oss.o: oss.c
	$(CC) $(CFLAGS) -c oss.c

clean:
	/bin/rm -f *.o user oss

