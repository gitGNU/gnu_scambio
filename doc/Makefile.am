AUTOMAKE_OPTIONS = foreign
doc_DATA = principes.html

principes.html: principes.txt
	asciidoc -a encoding=ISO-8859-1 $<

clean:
	rm -f principes.html
