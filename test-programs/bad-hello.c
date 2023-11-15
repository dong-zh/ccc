#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	printf("Hello world!\n");
	fprintf(stderr, "Hello from stderr\n");

	int const x[] = {0, 1};
	for (unsigned i = 0; i < 1000000000; ++i) {
		printf("i = %u\n", x[i]);
	}

	printf("This is perfectly fine :)\n");
}
