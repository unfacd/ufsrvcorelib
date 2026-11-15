/**
 * @file ufsrvscriptlib_mysql.h
 * @brief Native Lua bindings for the MySQL/MariaDB client.
 *
 * A thin `UfsrvScriptMysql*` wrapper over the MariaDB Connector/C (the
 * Lua-agnostic seam) plus `UfsrvScriptMysqlRegister()`, which builds the
 * `mysql` global table. The object model (connection/result userdata) is
 * adapted from LuaSQL's MySQL driver; see
 * dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_MYSQL_DESIGN.md.
 *
 * This is a deliberate addition to the ufsrvscriptlib dependency set: the mysql
 * binding links the system MariaDB Connector/C (shared libmariadb.so under
 * /opt — not vendored, unlike hiredis).
 */
#ifndef UFSRVSCRIPTLIB_MYSQL_H
#define UFSRVSCRIPTLIB_MYSQL_H

#include <uflib/uflib_defs.h>

#include <stddef.h>

#include <lua.h>
#include <mariadb/mysql.h>

/* Thin MySQL/MariaDB wrappers — the Lua-agnostic seam the bindings call. */
PUBLIC_API MYSQL               *UfsrvScriptMysqlConnect(const char *host, const char *user,
                                                        const char *password, const char *db,
                                                        unsigned int port, unsigned int ssl_verify);
PUBLIC_API int                  UfsrvScriptMysqlQuery(MYSQL *conn, const char *sql, size_t sql_len);
PUBLIC_API MYSQL_RES           *UfsrvScriptMysqlStoreResult(MYSQL *conn);
PUBLIC_API MYSQL_ROW            UfsrvScriptMysqlFetchRow(MYSQL_RES *res);
PUBLIC_API unsigned long       *UfsrvScriptMysqlFetchLengths(MYSQL_RES *res);
PUBLIC_API MYSQL_FIELD         *UfsrvScriptMysqlFetchFields(MYSQL_RES *res);
PUBLIC_API unsigned int         UfsrvScriptMysqlNumFields(MYSQL_RES *res);
PUBLIC_API unsigned long long   UfsrvScriptMysqlNumRows(MYSQL_RES *res);
PUBLIC_API unsigned int         UfsrvScriptMysqlFieldCount(MYSQL *conn);
PUBLIC_API unsigned long long   UfsrvScriptMysqlAffectedRows(MYSQL *conn);
PUBLIC_API unsigned long long   UfsrvScriptMysqlInsertId(MYSQL *conn);
PUBLIC_API unsigned int         UfsrvScriptMysqlErrno(MYSQL *conn);
PUBLIC_API const char          *UfsrvScriptMysqlError(MYSQL *conn);
PUBLIC_API char                *UfsrvScriptMysqlEscape(MYSQL *conn, const char *from, size_t from_len);
PUBLIC_API int                  UfsrvScriptMysqlPing(MYSQL *conn);
PUBLIC_API int                  UfsrvScriptMysqlCommit(MYSQL *conn);
PUBLIC_API int                  UfsrvScriptMysqlRollback(MYSQL *conn);
PUBLIC_API int                  UfsrvScriptMysqlAutocommit(MYSQL *conn, int mode);
PUBLIC_API void                 UfsrvScriptMysqlFreeResult(MYSQL_RES *res);
PUBLIC_API void                 UfsrvScriptMysqlClose(MYSQL *conn);

/**
 * @brief Build the `mysql` global table on the given lua_State.
 *
 * Call after UfsrvScriptBootstrap() and before running any script. The module
 * table is set via lua_setglobal (C-level), which strict.lua exempts.
 */
PUBLIC_API void UfsrvScriptMysqlRegister(lua_State *L);

#endif /* UFSRVSCRIPTLIB_MYSQL_H */
