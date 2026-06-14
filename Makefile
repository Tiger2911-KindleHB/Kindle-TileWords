APP := tilewords
EXT := extension/tilewords
BUILD := build
DIST := dist
ifeq ($(origin CC),default)
CC := arm-kindlehf-linux-gnueabihf-gcc
endif
CFLAGS ?= -std=c99 -Os -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections

SRC := src/tilewords.c
BIN := $(EXT)/bin/$(APP)

.PHONY: all clean package

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(EXT)/bin
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)
	chmod 755 $(BIN)


package: all
	mkdir -p $(DIST)
	chmod 755 $(EXT)/tilewords.sh
	cd extension && zip -r ../$(DIST)/tilewords-kual.zip tilewords -x 'tilewords/data/save.json.tmp'

clean:
	rm -rf $(BUILD) $(DIST) $(BIN)
