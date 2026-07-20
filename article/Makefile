# type "make" command in Unix to create the pdf file
PYTHON ?= python3

all: figures
	latex lawn
	bibtex lawn
	latex lawn
	latex lawn
	ps2pdf lawn.ps lawn.pdf

# Regenerate all data figures from the committed benchmark CSVs.
figures:
	$(PYTHON) src/make_figures.py

clean:
	(rm -rf *.ps *.log *.dvi *.aux *.*% *.lof *.lop *.lot *.toc *.idx *.ilg *.ind *.bbl *blg)
