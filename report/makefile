CUSTOM_FILE_0 := report.md.pdf
CUSTOM_FLAGS_0 := --csl=apa.csl --bibliography=bib.bib -H config.sty -V papersize:"a4paper" -A appendices.md.tex

PDFS := report.md.pdf
TEX := appendices.md.tex

all : $(TEX) $(PDFS)

$(CUSTOM_FILE_0) : CUSTOM_FLAGS := $(CUSTOM_FLAGS_0)

%.md.pdf : %.md
	pandoc $(CUSTOM_FLAGS) --latex-engine=xelatex $< -o $@

%.md.tex : %.md
	pandoc $< -o $@

clean :
	rm $(PDFS)

rebuild : clean all
