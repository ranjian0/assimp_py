all:
	@python setup.py build
	
develop:
	@python setup.py develop

.PHONY: clean

clean:
	@rm -rf build