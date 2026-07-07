PROG = config.cgi
SRCS = config.c

PKGS = fcgi
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS)) -lpthread
LDFLAGS += -Wl,-z,noexecstack

CFLAGS += -Wall -Wextra

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(PROG) *.o