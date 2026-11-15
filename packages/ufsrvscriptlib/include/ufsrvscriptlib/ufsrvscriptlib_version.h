/**
 * @file ufsrvscriptlib_version.h
 * @brief Version macros and accessor declarations for the ufsrvscriptlib engine.
 *
 * MAJOR/MINOR/PATCH are maintained manually here. The function bodies are
 * generated at build time by src/version.c.sh → version.c (a fresh compilation
 * timestamp on every build), mirroring the ufsrvcli version pipeline.
 */
#ifndef UFSRVSCRIPTLIB_VERSION_H
#define UFSRVSCRIPTLIB_VERSION_H

#define UFSRVSCRIPTLIB_MAJOR 0
#define UFSRVSCRIPTLIB_MINOR 1
#define UFSRVSCRIPTLIB_PATCH 0

const char *UfsrvScriptVersion(void)      __attribute__((const));
const char *UfsrvScriptVersionMajor(void) __attribute__((const));
const char *UfsrvScriptVersionMinor(void) __attribute__((const));
const char *UfsrvScriptVersionPatch(void) __attribute__((const));
const char *UfsrvScriptCompiledTimestamp(void) __attribute__((const));
const char *UfsrvScriptCompiledBy(void)        __attribute__((const));

#endif /* UFSRVSCRIPTLIB_VERSION_H */
