all:
	gcc -g -Wall -Werror -lm client_dir/deliver.c -o client_dir/deliver
	gcc -g -Wall -Werror -lm server_dir/server.c -o server_dir/server

clean:
	rm -rf client_dir/deliver
	rm -rf client_dir/deliver.o
	rm -rf server_dir/server
	rm -rf server_dir/server.o
	rm -rf server_dir/*.txt
	rm -rf server_dir/*.jpg
