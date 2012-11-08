INSTALL_PATH ?= $(CURDIR)
CC = gcc
# OPT ?= -O2 -DNDEBUG       # (A) Production use (optimized mode)
OPT ?= -g2              # (B) Debug mode, w/ full line-level debugging symbols
LDFLAGS=-lpthread
CFLAGS += -I. -I./include $(OPT) #$(PLATFORM_CCFLAGS) 

LIBOBJECTS = mempool.o util_os.o
mempool_unit_test:mempool_unit_test.o $(LIBOBJECTS)
	$(CC) mempool_unit_test.o $(LIBOBJECTS) -o $@ $(LDFLAGS) $(CFLAGS) 
%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm *.o