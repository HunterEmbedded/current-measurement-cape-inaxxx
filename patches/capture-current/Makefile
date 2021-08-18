CFLAGS = -Wall -g -pthread -lrt
PROGS = capture-current-iio

all: $(PROGS)

capture-current-iio: capture-current-iio.c iio_utils.c sql-manager.c
	$(CC) $(CFLAGS) -o $@ $^ -lsqlite3

clean:
	rm -f *.o
	rm -f $(PROGS)

install:
	cp $(PROGS) $(TARGETDIR)/
