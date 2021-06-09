all:
	@python setup.py build 

.PHONY: clean

clean:
	@rm -rf build