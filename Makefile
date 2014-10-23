BIN = gsconf
LIBS = -lssh2 -lreadline -lpq
CFLAGS = -pipe -Werror -Wall -Wextra -Wno-unused -g -I `pg_config --includedir`
LDFLAGS =

SRC = $(wildcard *.c)
OBJ = $(patsubst %.c,.tmp/%.o,$(SRC))
DEP = $(patsubst %.c,.tmp/%.d,$(SRC))
TMPDIR = .tmp

.PHONY: all clean

all: $(TMPDIR) $(BIN)

clean:
	@printf "   \033[38;5;154mCLEAN\033[0m\n"
	@rm -f $(BIN) $(TMPDIR)/*.d $(TMPDIR)/*.o

# rule for creating final binary
$(BIN): $(OBJ)
	@printf "   \033[38;5;69mLD\033[0m        $@\n"
	@$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $(BIN)

# rule for creating object files
$(OBJ) : $(TMPDIR)/%.o : %.c
	@printf "   \033[38;5;33mCC\033[0m        $(<:.c=.o)\n"
	@$(CC) $(CFLAGS) -std=gnu99 -MMD -MF $(TMPDIR)/$(<:.c=.d) -MT $@ -o $@ -c $<
	@cp -f $(TMPDIR)/$*.d $(TMPDIR)/$*.d.tmp
	@sed -e 's/.*://' -e 's/\\$$//' < $(TMPDIR)/$*.d.tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(TMPDIR)/$*.d
	@rm -f $(TMPDIR)/$*.d.tmp

$(TMPDIR):
	@mkdir $(TMPDIR)

# include dependency files
ifneq ($(MAKECMDGOALS),clean)
-include $(DEP)
endif
