doc_DATA = principes.html smtp.html files.html mdird.html

SUFFIXES = html txt

.txt.html:
	asciidoc -a encoding=ISO-8859-1 -o $@ $<

clean:
	rm -f $(doc_DATA)
