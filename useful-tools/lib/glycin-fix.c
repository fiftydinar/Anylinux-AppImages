#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

typedef void* (*gly_loader_new_t)(void*);
typedef void* (*gly_loader_new_for_stream_t)(void*);
typedef void* (*gly_loader_new_for_bytes_t)(void*);
typedef void (*gly_loader_set_sandbox_selector_t)(void*, int);

#ifndef GLY_SANDBOX_SELECTOR_NOT_SANDBOXED
#define GLY_SANDBOX_SELECTOR_NOT_SANDBOXED 3
#endif

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
