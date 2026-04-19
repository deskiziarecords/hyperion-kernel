CC      = gcc
SRCS    = src/main.c src/pattern.c src/eval.c src/entropy.c \
          src/memory.c src/geometry.c src/risk.c src/oanda.c \
          src/config.c src/log.c src/backtest.c
INCS    = include/smart.h
TARGET  = smart

CFLAGS  = -std=c99 -Os -flto \
           -ffunction-sections -fdata-sections \
           -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Wpedantic \
           -Iinclude

LDFLAGS = -Wl,--gc-sections -Wl,--strip-all -lm

.PHONY: all clean test backtest size testdata debug

all: $(TARGET)

$(TARGET): $(SRCS) $(INCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	strip --strip-all $@
	@echo "Size: $$(du -h $(TARGET) | cut -f1)"

debug: $(SRCS) $(INCS)
	$(CC) -std=c99 -g -O0 -Wall -Wextra -Iinclude -fsanitize=address,undefined $(SRCS) -o $(TARGET)_debug -lm
	@echo "Debug build: $(TARGET)_debug"

test: $(TARGET)
	mkdir -p logs
	./$(TARGET) --once --sequence "BBBIBB" --verbose
	@echo ""
	./$(TARGET) --once --sequence "BIXWUDX" --verbose

backtest: $(TARGET)
	mkdir -p logs
	./$(TARGET) --backtest $(CSV) --verbose

size: $(TARGET)
	@echo "Binary size:" && du -h $(TARGET)
	@size $(TARGET)

testdata:
	python3 -c "\
import random, time; \
print('timestamp_ms,open,high,low,close,volume'); \
p=1.34500; t=int(time.time()*1000); \
[print(f'{t+i*60000},{p*(1+random.gauss(0,.0003)):.5f},{p*1.001:.5f},{p*0.999:.5f},{p*(1+random.gauss(0,.0003)):.5f},{random.randint(500,5000)}') for i in range(500)]" > test_data.csv
	@echo "Generated test_data.csv"

clean:
	rm -f $(TARGET) $(TARGET)_debug *.o
