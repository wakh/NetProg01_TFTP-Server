#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv){

	int startPort;
	int endPort;

	if(argc != 3){
		printf("Not correct amount of arguments\n");
		return EXIT_FAILURE;
	}

	startPort = atoi(argv[1]);
	endPort = atoi(argv[2]);

	if(startPort > endPort){
		printf("Startport is before endPort\n");
		return EXIT_FAILURE;
	}

	printf("%d %d\n", startPort, endPort);
	
	return EXIT_SUCCESS;
}