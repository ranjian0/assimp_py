all: clean install test
	
develop:
	@pip install -e .

install:
	@pip install . -v

test:
	@pytest tests/

profile:
	python scripts/memprof.py

.PHONY: clean

clean:
	@rm -f *.so
	@rm -rf dist
	@rm -rf build
	@rm -rf src/*.egg-info
	@rm -rf .pytest_cache
	@rm -rf tests/__pycache__