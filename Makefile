CC = clang
CFLAGS = -Wall -g -I/opt/homebrew/include -I/opt/homebrew/opt/curl/include
LDFLAGS = -L/opt/homebrew/lib -L/opt/homebrew/opt/curl/lib -lcurl -lcjson
TARGET = ai_agent
SRCS = main.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
