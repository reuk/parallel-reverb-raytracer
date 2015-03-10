PDFS := $(patsubst %.md,%.md.pdf,$(wildcard *.md))
MAN := $(patsubst %.md,%,$(wildcard *.md))

all : $(PDFS) $(MAN)

%.md.pdf : %.md
	pandoc -H config.sty --latex-engine=xelatex $< -o $@

% : %.md
	pandoc -s -t man $< -o $@

clean :
	rm $(PDFS)
	rm $(MAN)

rebuild : clean all
