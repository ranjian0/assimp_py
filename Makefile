all: clean install test
	
develop:
	@python setup.py develop

install:
	@python setup.py install 

test:
	@pytest tests/

.PHONY: clean

clean:
	@rm -f *.so
	@rm -rf dist
	@rm -rf build
	@rm -rf *.egg-info
	@rm -rf .pytest_cache