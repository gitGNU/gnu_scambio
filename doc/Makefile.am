SUFFIXES = html txt
doc_DATA = principes.html smtp.html files.html mdird.html

.txt.html:
	asciidoc -a encoding=ISO-8859-1 -o $@ $<

clean-local:
	rm -f $(doc_DATA)
