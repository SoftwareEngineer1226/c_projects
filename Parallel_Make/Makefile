OBJS_DIR = .objs

# define all the target executables
EXE_PARMAKE = parmake
EXES_TARGET = $(EXE_PARMAKE)

# list object file dependencies for each
OBJS_PARMAKE = parmake.o parser.o rule.o parmake_main.o format.o

INC = -I./includes/
WARNINGS = -Wall -Wextra -Werror -Wno-error=unused-parameter


CC = gcc
CFLAGS_COMMON = $(WARNINGS) $(INC) -std=c99 -c -MMD -MP -D_GNU_SOURCE -DTHREAD_SAFE -pthread
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O2
CFLAGS_DEBUG = $(CFLAGS_COMMON) -O0 -g -DDEBUG

CFLAGS_TSAN_DEBUG = $(CFLAGS_DEBUG) -fsanitize=thread -DSANITIZE_THREADS
CFLAGS_TSAN = $(CFLAGS_TSAN_DEBUG) -UDEBUG


# set up linker
PROVIDED_LIBRARIES:=$(shell find libs/ -type f -name '*.a' 2>/dev/null)
PROVIDED_LIBRARIES:=$(PROVIDED_LIBRARIES:libs/lib%.a=%)

LD = $(CC)
LDFLAGS = -pthread -Llibs/ $(foreach lib,$(PROVIDED_LIBRARIES),-l$(lib)) -lm
LDFLAGS_TSAN = -ltsan $(LDFLAGS)

.PHONY: all
all: release

# build types
# run clean before building debug so that all of the release executables
# disappear
.PHONY: release
.PHONY: debug
.PHONY: tsan
.PHONY: debug-tsan

release: $(EXES_TARGET)
debug: clean $(EXES_TARGET:%=%-debug)
tsan: clean $(EXES_TARGET:%=%-tsan)
debug-tsan: clean $(EXES_TARGET:%=%-debug-tsan)

# include dependencies
-include $(OBJS_DIR)/*.d

$(OBJS_DIR):
	@mkdir -p $(OBJS_DIR)

# patterns to create objects
# keep the debug and release postfix for object files so that we can always
# separate them correctly
$(OBJS_DIR)/%-debug.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_DEBUG) $< -o $@

$(OBJS_DIR)/%-debug-tsan.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_TSAN_DEBUG) $< -o $@

$(OBJS_DIR)/%-tsan.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_TSAN) $< -o $@

$(OBJS_DIR)/%-release.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_RELEASE) $< -o $@

# exes
# you will need a triple of exe and exe-debug and exe-tsan for each exe (other
# than provided exes)
$(EXE_PARMAKE): $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-release.o)
	$(LD) $^ $(LDFLAGS) -o $@

$(EXE_PARMAKE)-debug: $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-debug.o)
	$(LD) $^ $(LDFLAGS) -o $@

$(EXE_PARMAKE)-debug-tsan: $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-debug-tsan.o)
	$(LD) $^ $(LDFLAGS_TSAN) -o $@

$(EXE_PARMAKE)-tsan: $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-tsan.o)
	$(LD) $^ $(LDFLAGS_TSAN) -o $@

.PHONY: clean
clean:
	-rm -rf .objs $(EXES_TARGET)\
		$(EXES_TARGET:%=%-tsan)\
		$(EXES_TARGET:%=%-debug)\
		$(EXES_TARGET:%=%-debug-tsan)
