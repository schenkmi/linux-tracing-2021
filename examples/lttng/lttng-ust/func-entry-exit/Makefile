TEST=test
INSTALL=install
CC=gcc
CXX=g++

CFLAGS=-O2 -g -finstrument-functions
LDADD=

OBJS=func-entry-exit.o
BINARY=func-entry-exit

####################################################################################
# Dependencies generation defs
####################################################################################
DEPDIR=./.deps
DEPFLAGS=-MD -MF $(DEPDIR)/$(patsubst %.o,%.d,$@)

####################################################################################
# Build rules
####################################################################################
%.o : %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
	@cd $(DEPDIR); cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

.PHONY: all
all: depdir $(BINARY)

depdir:
	@$(TEST) -d $(DEPDIR) || $(INSTALL) -d -m 775 $(DEPDIR)

$(BINARY): $(OBJS)
	$(CC) -o $@ $^ $(LDADD)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)
	rm -rf .deps

-include $(patsubst %.o,$(DEPDIR)/%.P,$(depobj))

