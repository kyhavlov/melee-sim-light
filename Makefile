.PHONY: build test preprocess validate build_data

PY := uv run python
DATASETS_DIR ?= datasets
SUITE ?= replays/suites/fox_falco_fd_ucf084_recent.json
CHUNK ?= 4096
OUT ?=

ifneq ($(strip $(OUT)),)
VALIDATE_OUT := --out $(OUT)
endif

build:
	@$(PY) python/setup.py build_ext --inplace

test: build
	@$(PY) -m pytest

preprocess:
	@$(PY) -m tools.slippi.preprocess_suite --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)"

validate:
	@$(PY) -m tools.eval.run_one_step_suite_eval --suite "$(SUITE)" --datasets-dir "$(DATASETS_DIR)" --chunk "$(CHUNK)" $(VALIDATE_OUT)

build_data:
	@$(PY) -m tools.extraction.build_data --iso-dir _iso --stage grnba --chars fox,falco
