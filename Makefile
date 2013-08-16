PROG = rdf_plot

CC   = gcc
CFLAGS = -O2 -march=native -Wall -W
LDFLAGS = -lgd

OBJS = main.o

$(PROG): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(PROG) *.o

.PHONY: all clean
