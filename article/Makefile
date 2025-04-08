# type "make" command in Unix to create asme2ej.pdf file 
all:
	latex lawn
	bibtex lawn
	latex lawn
	latex lawn
	ps2pdf lawn.ps lawn.pdf

clean:
	(rm -rf *.ps *.log *.dvi *.aux *.*% *.lof *.lop *.lot *.toc *.idx *.ilg *.ind *.bbl *blg)
