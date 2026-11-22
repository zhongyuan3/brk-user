/*
 * Crt0 for dynamically linked executables: linked into the program
 * itself, so it runs after /lib/ld-brk.so has mapped libc.so and
 * resolved everything.  The BRK register convention hands argc,
 * argv and envp over in a0..a2.
 */
extern char **environ;
extern int main(int argc, char **argv, char **envp);
extern void exit(int status) __attribute__((noreturn));

void _start(long argc, char **argv, char **envp)
{
	environ = envp;
	exit(main((int)argc, argv, envp));
}
