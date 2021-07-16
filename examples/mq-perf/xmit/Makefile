INSTALL=install
TEST=test
CXX=g++

CFLAGS=-O2 -g -pthread -finstrument-functions -I../shmemq
LDADD=-pthread -lrt

$(info CFLAGS : $(CFLAGS))
$(info LDADD  : $(LDADD))

OBJS=mq-perf-xmit.o ../shmemq/shmemq.o
BINARY=mq-perf-xmit


####################################################################################
# Dependencies generation defs
####################################################################################
DEPDIR=./.deps
DEPFLAGS=-MD -MF $(DEPDIR)/$(patsubst %.o,%.d,$@)

####################################################################################
# Build rules
####################################################################################
%.o : %.cpp
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
	@cd $(DEPDIR); cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

%.o : %.cc
	$(CXX) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
	@cd $(DEPDIR); cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

.PHONY: all
all: depdir $(BINARY)

depdir:
	@$(TEST) -d $(DEPDIR) || $(INSTALL) -d -m 775 $(DEPDIR)
	@$(TEST) -d shmemq/$(DEPDIR) || $(INSTALL) -d -m 775 shmemq/$(DEPDIR)

$(BINARY): $(OBJS)
	$(CXX) -o $@ $^ $(LDADD)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)
	rm -rf .deps
	rm -rf shmemq

-include $(patsubst %.o,$(DEPDIR)/%.P,$(depobj))
