all: client.cpp server.cpp
	g++ client.cpp -o client -pthread
	g++ server.cpp -o server -pthread
clean:
	rm -f client
	rm -f server
