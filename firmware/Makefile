IDF_PATH ?= $(HOME)/esp/esp-idf

define IDF_ENV
	export IDF_PATH="$(IDF_PATH)"; \
	export IDF_PATH_FORCE=1; \
	. "$(IDF_PATH)/export.sh";
endef

.PHONY: config build flash run dump clean

config:
	@$(IDF_ENV) idf.py menuconfig

build:
	@$(IDF_ENV) idf.py build $(IDF_FLAGS)

flash: build
	@$(IDF_ENV) idf.py -b 921600 flash

run: flash
	@$(IDF_ENV) idf.py monitor

dump:
	@$(IDF_ENV) xtensa-esp-elf-objdump -dS build/lookout.elf

clean:
	rm -rf build
