SUFFIXES: htm html
doc_DATA = style.css index.html faq.html doc.html contact.html

HEADER=$(top_srcdir)/www/header.htm
FOOTER=$(top_srcdir)/www/footer.htm

$(doc_DATA): $(HEADER) $(FOOTER)

.htm.html:
	cat $(HEADER) $< $(FOOTER) > $@

