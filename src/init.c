#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *logo[] = {
	"______   _______     ___  ____   ",
	"|_   _ \\ |_   __ \\   |_  ||_  _|  ",
	"  | |_) |  | |__) |    | |_/ /     ",
	"  |  __'.  |  __ /     |  __'.     ",
	" _| |__) |_| |  \\ \\_  _| |  \\ \\_  ",
	"|_______/|____| |___||____||____|  ",
	"					  ",
	"BRK (Barely Running Kernel) v0.0.1",
	"",
	"This is free software; see the source for copying conditions.  There is NO",
	"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.",
};

static void print_logo(void)
{
	for (size_t i = 0; i < sizeof(logo) / sizeof(logo[0]); i++)
		puts(logo[i]);
}

int main(void)
{
	print_logo();
	while (1) {
		pid_t cpid = fork();
		if (cpid < 0) {
			perror("fork failed");
			goto fail;
		}

		if (cpid == 0) {
			char *argv[] = { "sh", 0 };
			execvp(argv[0], argv);
			perror("execve failed");
			_exit(1);
		}

		while (1) {
			pid_t wpid = wait(0);
			if (wpid == cpid) {
				printf("\n\nPress any key to restart shell\n");
				getchar();
				break;
			}
		}
	}

fail:
	fprintf(stderr, "failed to start shell\n");
	while (1)
		wait(0);
}
