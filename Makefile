CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -Iinclude -pthread
LDFLAGS = -pthread

BIN_DIR = bin
SRC_DIR = src
TEST_DIR = tests

CORE_SOURCES = \
	$(SRC_DIR)/common.c \
	$(SRC_DIR)/sync_tracer.c \
	$(SRC_DIR)/task_queue.c \
	$(SRC_DIR)/thread_pool.c \
	$(SRC_DIR)/sync_labs.c

MAIN_SOURCES = \
	$(SRC_DIR)/main.c \
	$(CORE_SOURCES) \
	$(SRC_DIR)/ui.c \
	$(SRC_DIR)/web_server.c

HEADERS = \
	include/common.h \
	include/sync_tracer.h \
	include/task_queue.h \
	include/thread_pool.h \
	include/sync_labs.h \
	include/ui.h \
	include/web_server.h \
	$(SRC_DIR)/web_assets.h

MAIN_TARGET = $(BIN_DIR)/DACK_project

TESTS = thread_basic race_condition mutex_demo condition_demo semaphore_demo
TEST_TARGETS = $(addprefix $(BIN_DIR)/,$(TESTS))

.PHONY: all clean run web run_demos test tsan asan

all: $(BIN_DIR) $(MAIN_TARGET) $(TEST_TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(MAIN_TARGET): $(MAIN_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(MAIN_SOURCES) -o $@ $(LDFLAGS)

$(BIN_DIR)/%: $(TEST_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./$(MAIN_TARGET)

web: all
	./$(MAIN_TARGET) --web 8080

run_demos: all
	@echo "=== thread_basic ===" && ./$(BIN_DIR)/thread_basic
	@echo "=== race_condition ===" && ./$(BIN_DIR)/race_condition
	@echo "=== mutex_demo ===" && ./$(BIN_DIR)/mutex_demo
	@echo "=== condition_demo ===" && ./$(BIN_DIR)/condition_demo
	@echo "=== semaphore_demo ===" && ./$(BIN_DIR)/semaphore_demo

test: $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_DIR)/unit_stress.c $(CORE_SOURCES) -o $(BIN_DIR)/unit_stress $(LDFLAGS)
	./$(BIN_DIR)/unit_stress

tsan: $(BIN_DIR)
	$(CC) -Wall -Wextra -Werror -std=c11 -Iinclude -fsanitize=thread -g -pthread $(TEST_DIR)/unit_stress.c $(CORE_SOURCES) -o $(BIN_DIR)/unit_stress_tsan -pthread
	./$(BIN_DIR)/unit_stress_tsan

asan: $(BIN_DIR)
	$(CC) -Wall -Wextra -Werror -std=c11 -Iinclude -fsanitize=address,undefined -g -pthread $(TEST_DIR)/unit_stress.c $(CORE_SOURCES) -o $(BIN_DIR)/unit_stress_asan -pthread
	./$(BIN_DIR)/unit_stress_asan

clean:
	rm -rf $(BIN_DIR)
