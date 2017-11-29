all:
	$(MAKE) -C ./src all

test:
	$(MAKE) -C ./src $@

clean:
	$(MAKE) -C ./src $@

distclean:
	$(MAKE) -C ./src $@
.PHONY: distclean

package: all
	$(MAKE) -C ./src package
.PHONY: package

buildall:
	$(MAKE) -C ./src $@

rebuild: clean all