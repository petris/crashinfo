INPUTDIR := inputdir
PROVE := prove

test: $(INPUTDIR)/core $(INPUTDIR)/proc
	PERL5LIB=$(PERL5LIB):. $(PROVE) *.t

$(INPUTDIR)/crash: crash.c
	mkdir -p $(INPUTDIR)
	$(CC) -Wall -o $@ $< -lpthread -lcoredumper

$(INPUTDIR)/%ore $(INPUTDIR)/pro%: $(INPUTDIR)/crash
	cd $(INPUTDIR) && ./crash
