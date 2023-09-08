#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	int * ints = malloc(10 * sizeof(int));

	for (int i = 0; i < 10; i++) {
		ints[i] = i + 1;
	}

	for (int i = 0; i < 10; i++) {
		printf("%d\n",ints[i]);
	}

}