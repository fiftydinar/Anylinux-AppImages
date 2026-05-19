#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void* (*gly_loader_new_t)(void*);
typedef void* (*gly_loader_new_for_stream_t)(void*);
typedef void* (*gly_loader_new_for_bytes_t)(void*);
typedef void (*gly_loader_set_sandbox_selector_t)(void*, int);
typedef int (*execvp_t)(const char*, char *const[]);
typedef int (*execvpe_t)(const char*, char *const[], char *const[]);

#ifndef GLY_SANDBOX_SELECTOR_NOT_SANDBOXED
#define GLY_SANDBOX_SELECTOR_NOT_SANDBOXED 3
#endif

extern char **environ;

static char saved_appdir[4096] = "";

__attribute__((constructor))
static void capture_appdir(void) {
	const char *a = getenv("APPDIR");
	if (a)
		strncpy(saved_appdir, a, sizeof(saved_appdir) - 1);
}

static void* get_real(const char *name) {
	void *sym = dlsym(RTLD_NEXT, name);
	if (!sym) sym = dlsym(RTLD_DEFAULT, name);
	return sym;
}

static void force_not_sandboxed(void *loader) {
	static gly_loader_set_sandbox_selector_t setter = NULL;
	if (!setter) setter = (gly_loader_set_sandbox_selector_t)
		get_real("gly_loader_set_sandbox_selector");
	if (setter && loader)
		setter(loader, GLY_SANDBOX_SELECTOR_NOT_SANDBOXED);
}

void* gly_loader_new(void* file) {
	static gly_loader_new_t real = NULL;
	if (!real) real = (gly_loader_new_t)get_real("gly_loader_new");
	void *loader = real ? real(file) : NULL;
	if (loader) force_not_sandboxed(loader);
	return loader;
}

void* gly_loader_new_for_stream(void* stream) {
	static gly_loader_new_for_stream_t real = NULL;
	if (!real) real = (gly_loader_new_for_stream_t)get_real("gly_loader_new_for_stream");
	void *loader = real ? real(stream) : NULL;
	if (loader) force_not_sandboxed(loader);
	return loader;
}

void* gly_loader_new_for_bytes(void* bytes) {
	static gly_loader_new_for_bytes_t real = NULL;
	if (!real) real = (gly_loader_new_for_bytes_t)get_real("gly_loader_new_for_bytes");
	void *loader = real ? real(bytes) : NULL;
	if (loader) force_not_sandboxed(loader);
	return loader;
}

static const char *base_name(const char *path) {
	if (!path) return NULL;
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static int is_glycin_loader(const char *name) {
	return name && strncmp(name, "glycin-", 7) == 0;
}

/* Try to exec a glycin loader from $APPDIR/bin.
 * Returns 0 on success, -1 on failure (errno is set). */
static int try_appdir_exec(const char *file, char *const argv[], char *const envp[]) {
	if (!saved_appdir[0]) return -1;

	size_t appdir_len = strlen(saved_appdir);
	size_t flen = strlen(file);
	char full_path[appdir_len + 6 + flen];
	snprintf(full_path, sizeof(full_path), "%s/bin/%s", saved_appdir, file);

	if (access(full_path, X_OK) != 0) return -1;

	/* Use execvpe so the child env is correct; but we embed PATH so
	 * the loader itself can find its own deps if needed. */
	/* Build an env that includes PATH=APPDIR/bin */
	size_t env_count = 0;
	if (envp) while (envp[env_count]) env_count++;

	char **new_env = calloc(env_count + 2, sizeof(char*));
	if (!new_env) return -1;

	size_t j = 0;
	for (size_t i = 0; i < env_count; i++) {
		if (strncmp(envp[i], "PATH=", 5) != 0)
			new_env[j++] = (char*)envp[i];
	}

	char path_var[appdir_len + 10];
	snprintf(path_var, sizeof(path_var), "PATH=%s/bin", saved_appdir);
	new_env[j++] = strdup(path_var);
	new_env[j] = NULL;

	execvpe_t real_vpe = (execvpe_t)dlsym(RTLD_NEXT, "execvpe");
	if (!real_vpe) real_vpe = (execvpe_t)dlsym(RTLD_DEFAULT, "execvpe");

	int ret = -1;
	if (real_vpe)
		ret = real_vpe(full_path, argv, (char *const *)new_env);

	int saved = errno;
	for (size_t i = env_count; i < j; i++)
		free(new_env[i]);
	free(new_env);
	errno = saved;
	return ret;
}

int execvp(const char *file, char *const argv[]) {
	static execvp_t real = NULL;
	if (!real) real = (execvp_t)dlsym(RTLD_NEXT, "execvp");

	const char *name = base_name(file);

	if (is_glycin_loader(name) && strchr(file, '/') == NULL) {
		int ret = try_appdir_exec(name, argv, environ);
		if (ret == 0 || errno != ENOENT)
			return ret;
		/* fall through to real execvp if the file doesn't exist in APPDIR */
	}

	return real ? real(file, argv) : -1;
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
	static execvpe_t real = NULL;
	if (!real) real = (execvpe_t)dlsym(RTLD_NEXT, "execvpe");

	const char *name = base_name(file);

	if (is_glycin_loader(name) && strchr(file, '/') == NULL) {
		int ret = try_appdir_exec(name, argv, envp);
		if (ret == 0 || errno != ENOENT)
			return ret;
	}

	return real ? real(file, argv, envp) : -1;
}
