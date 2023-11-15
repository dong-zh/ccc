#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>

static char const *const COMPILED_APP_NAME = "a.out";

extern char *const *const environ;

#define DEBUG (false)

typedef struct DccArgs {
	char **args;
	size_t const length;
} DccArgs;

static DccArgs constructDccArgs(int const argc, char const *const argv[]);
static void freeDccArgs(DccArgs *args);
static bool spawnDcc(DccArgs const *args);
static void spawnCompiledApp(void);
static bool findError(char const stderrOutput[]);
static void pretendDelete(void);
static void ignoreSignals(void);
static void fakeTemp(void);

int main(int const argc, char const *const argv[])
{
	printf("===\n"
	       "Welcome to ccc! A compiler that compiles your C file, and "
	       "then runs it.\n"
	       "If it encounters any undefined behaviour, it deletes all your "
	       "files! How exciting!\n"
	       "===\n\n");

	if (argc < 2) {
		fprintf(stderr,
			"===\nHow to use:\n~z5257526/ccc <your C file>\n===\n");
		return EXIT_FAILURE;
	}

	DccArgs dccArgs = constructDccArgs(argc, argv);
	bool const dccOk = spawnDcc(&dccArgs);

	if (dccOk) {
		printf("==< Your program compiled! Now let's run it >:) >==\n\n");
		spawnCompiledApp();
	} else {
		printf("==< YOUR PROGRAM FAILED TO COMPILE. GOODBYE. >==\n");
		pretendDelete();
	}
	freeDccArgs(&dccArgs);
}

static DccArgs constructDccArgs(int const argc, char const *const argv[])
{
	static const size_t EXTRA_ARGS = 2;

	DccArgs args = { .args = calloc(argc + EXTRA_ARGS + 1, sizeof(char *)),
			 .length = argc + EXTRA_ARGS + 1 };

	if (!args.args)
		err(EXIT_FAILURE, "Failed to calloc DccArgs");

	args.args[0] = strdup("dcc");
	args.args[1] = strdup("-o");
	args.args[2] = strdup(COMPILED_APP_NAME);
	for (unsigned i = EXTRA_ARGS + 1; i < EXTRA_ARGS + argc; ++i) {
		args.args[i] = strdup(argv[i - EXTRA_ARGS]);
	}
	args.args[args.length - 1] = NULL;

	if (DEBUG) {
		printf("args:\n");
		for (unsigned i = 0; i < args.length; ++i) {
			printf("\t%s\n", args.args[i]);
		}
	}
	return args;
}

static void freeDccArgs(DccArgs *args)
{
	// Last element is a NULL
	for (size_t i = 0; i < args->length - 1; ++i) {
		free(args->args[i]);
	}
	free(args->args);
}

static bool spawnDcc(DccArgs const *args)
{
	pid_t pid = -1;
	int error = posix_spawnp(&pid, args->args[0], NULL, NULL, args->args,
				 environ);
	if (error) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't spawn %s", args->args[0]);
	}

	int exitStatus;
	if (waitpid(pid, &exitStatus, 0) == -1) {
		err(EXIT_FAILURE, "Couldn't wait for %s, (PID %d)",
		    args->args[0], pid);
	}

	if (DEBUG) {
		printf("%s exited with status %d.\n", args->args[0],
		       exitStatus);
	}
	return exitStatus == 0;
}

static void spawnCompiledApp(void)
{
	// Pipe the child's `stderr` back here
	// [0] is read, [1] is write
	int pipeFd[2];
	if (pipe(pipeFd) == -1)
		err(EXIT_FAILURE, "Couldn't create pipe");

	// Create file actions
	posix_spawn_file_actions_t fileActions;
	int error = posix_spawn_file_actions_init(&fileActions);
	if (error) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't create file actions");
	}

	// Replace the child's `stderr` with the write end of the pipe
	if ((error = posix_spawn_file_actions_adddup2(
		     &fileActions, pipeFd[STDOUT_FILENO], STDERR_FILENO))) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't adddup2");
	}

	// Tell the child to close the read end of the pipe
	if ((error = posix_spawn_file_actions_addclose(&fileActions,
						       pipeFd[STDIN_FILENO]))) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't addclose pipefd 0");
	}

	// Now that we've dup2-ed, tell the child to close the write end
	if ((error = posix_spawn_file_actions_addclose(
		     &fileActions, pipeFd[STDOUT_FILENO]))) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't addclose pipefd 1");
	}

	// Args for the child
	// +2 for "./" and +1 for NULL
	char childPath[strlen(COMPILED_APP_NAME) + 3];
	snprintf(childPath, sizeof(childPath), "./%s", COMPILED_APP_NAME);
	char *const args[] = { childPath, NULL };

	pid_t pid = -1;
	error = posix_spawn(&pid, args[0], &fileActions, NULL, args, environ);
	if (error) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't spawn %s", args[0]);
	}

	// Close the parent's write end of the pipe
	close(pipeFd[STDOUT_FILENO]);

	// Read the child's `stderr`
	FILE *childStderr = fdopen(pipeFd[STDIN_FILENO], "r");

	size_t maxOutputSize = 128;
	char *stderrOutput = calloc(maxOutputSize, sizeof(char));
	if (!stderrOutput)
		err(EXIT_FAILURE, "Failed to calloc child output buffer");

	size_t outputCount = 0;
	int readChar;
	while ((readChar = fgetc(childStderr)) != EOF) {
		if (outputCount >= maxOutputSize) {
			maxOutputSize *= 2;
			stderrOutput = realloc(stderrOutput, maxOutputSize);
			if (!stderrOutput)
				err(EXIT_FAILURE, "Failed to realloc buffer");
		}
		stderrOutput[outputCount] = readChar;
		++outputCount;
	}
	stderrOutput[outputCount] = '\0';

	if (DEBUG) {
		fprintf(stderr, "Child's stderr:\n");
		fprintf(stderr, "%s", stderrOutput);
		fprintf(stderr, "End child's stderr\n");
	}

	fclose(childStderr);

	int exitStatus;
	if (waitpid(pid, &exitStatus, 0) == -1) {
		err(EXIT_FAILURE, "Couldn't wait for %s, (PID %d)", args[0],
		    pid);
	}

	if (DEBUG) {
		printf("%s exited with status %d.\n", args[0], exitStatus);
	}
	if (findError(stderrOutput)) {
		printf("\n==< UNDEFINED BEHAVIOUR DETECTED. GOODBYE. >==\n");
		pretendDelete();
	} else {
		fprintf(stderr, "%s\n", stderrOutput);
		printf("==< No errors found, your're safe... for now :) >==\n");
	}
}

static bool findError(char const stderrOutput[])
{
	return (strstr(stderrOutput, "Runtime error") ||
		strstr(stderrOutput, "Execution terminated") ||
		strstr(stderrOutput, "Execution stopped") ||
		strstr(stderrOutput, "dcc-help"));
}

static void pretendDelete(void)
{
	ignoreSignals();
	// usleep(3000000);
	printf("==< DELETING YOUR FILES... >==\n");
	char const *const homePath = getenv("HOME");
	if (!homePath)
		errx(EXIT_FAILURE, "Couldn't get $HOME");

	DIR *const homeDir = opendir(homePath);
	if (!homeDir)
		err(EXIT_FAILURE, "Couldn't open %s", homePath);

	struct dirent *currentFile = NULL;
	while ((currentFile = readdir(homeDir))) {
		if (currentFile->d_name[0] == '.')
			// Ignore hidden files
			continue;

		printf("Deleting ~/%s\n", currentFile->d_name);
		usleep(200000);
	}
	closedir(homeDir);

	printf("\nTip: try writing correct code next time :)\n\n");
	fakeTemp();
	// printf("Lol jks :P\n");
}

static void ignoreSignals(void)
{
	for (int signal = 1; signal < 32; ++signal) {
		if (signal == SIGKILL || signal == SIGSTOP || signal == SIGHUP)
			continue;

		if (sigaction(signal,
			      &(struct sigaction){ .sa_handler = SIG_IGN },
			      NULL) == -1) {
			err(EXIT_FAILURE, "Couldn't set signal %d", signal);
		}
	}
}

static void fakeTemp(void)
{
	// Create a temp dir and change to it
	char tempDirTemplate[] = "/tmp/tmp.XXXXXX";
	char const *const tempDir = mkdtemp(tempDirTemplate);
	if (tempDir == NULL) {
		perror("Failed to create temporary directory");
		exit(EXIT_FAILURE);
	}
	if (chdir(tempDir) == -1) {
		perror("Failed to change directory to temporary directory");
		exit(EXIT_FAILURE);
	}

	// Set the user's $HOME to the temp dir
	char const *const front = "export HOME=";
	char buffer[strlen(front) + strlen(tempDir) + 1];
	snprintf(buffer, sizeof(buffer), "%s%s", front, tempDir);
	// printf("Running '%s'", buffer);
	if (setenv("HOME", tempDir, 1) == -1) {
		perror("Failed to set $HOME");
		exit(EXIT_FAILURE);
	}
	char *const userShell = getenv("SHELL");

	// posix_spawn the user shell
	pid_t pid = -1;
	int error = posix_spawnp(&pid, userShell, NULL, NULL,
				 (char *const[]){ userShell, NULL }, environ);
	if (error) {
		errno = error;
		err(EXIT_FAILURE, "Couldn't spawn %s", userShell);
	}

	// Wait for the user shell to exit
	int exitStatus;
	waitpid(pid, &exitStatus, 0);
}
