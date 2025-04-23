all: clean install test
	
develop:
	@pip install -e .

install:
	@pip install . -v

test:
	@pytest tests/

.PHONY: clean

clean:
	@rm -f *.so
	@rm -rf dist
	@rm -rf build
	@rm -rf *.egg-info
	@rm -rf .pytest_cache