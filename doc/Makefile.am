doc_DATA = principes.html

principes.html: principes.txt
	asciidoc -a encoding=ISO-8859-1 -o $@ $<

clean:
	rm -f principes.html
