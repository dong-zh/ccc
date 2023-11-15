# Define the compiler to use
CC = cc

# Define the flags to pass to the compiler
CFLAGS = -Wall -Wextra -Wpedantic

# Define the source file to compile
SRC = ccc.c

# Define the name of the output executable
OUT = ccc

# Define the default target to build
all: $(OUT)

# Define the rule to build the executable
$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

# Define a clean target to remove the executable
clean:
	rm -f $(OUT)

# Deploy the executable to my home directory and allow public execution
deploy: $(OUT)
	$(CC) $(CFLAGS) -O -o $(OUT) $(SRC)
	@cp $(OUT) ~/$(OUT)
	@chmod o+x ~/$(OUT)
