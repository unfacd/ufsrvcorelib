/**
 * @file ufsrvscriptlib_mysql.c
 * @brief Native Lua bindings for the MySQL/MariaDB client.
 *
 * Ports LuaSQL's MySQL driver object model — `connect(host, port, user,
 * password, database)` returning a connection userdata with `:query()` (which
 * yields a result userdata for SELECT and an affected-rows count otherwise) —
 * to the ufsrvscriptlib engine, over a thin `UfsrvScriptMysql*` wrapper around
 * the MariaDB Connector/C.
 *
 * Adaptations from LuaSQL (MIT):
 *  - the environment tier is dropped (mysql_library_end() is process-global and
 *    must not run per-object);
 *  - column values are type-aware (LuaSQL pushes everything as a string);
 *  - errors use the `nil, errstr, errno` convention;
 *  - Lua 5.4 API, no luaL_register shim.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_mysql.h>

#include <lauxlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UFSRVSCRIPTLIB_MYSQL_VERSION     "ufsrvscriptlib_mysql 0.1.0"
#define UFSRVSCRIPTLIB_MYSQL_COPYRIGHT   "Copyright (C) 2015-2026 unfacd works"
#define UFSRVSCRIPTLIB_MYSQL_DESCRIPTION "Lua bindings for the MySQL/MariaDB client (adapted from LuaSQL)"

#define UFSRVSCRIPTLIB_MYSQL_CONN_MT     "ufsrvscript.mysql.connection"
#define UFSRVSCRIPTLIB_MYSQL_RESULT_MT   "ufsrvscript.mysql.result"

/* ---------------------------------------------------------------------------
 * Thin MySQL/MariaDB wrappers (the Lua-agnostic seam).
 * ------------------------------------------------------------------------- */

MYSQL *
UfsrvScriptMysqlConnect(const char *host, const char *user, const char *password,
                        const char *db, unsigned int port, unsigned int ssl_verify)
{
    MYSQL *conn = mysql_init(NULL);
    my_bool verify = ssl_verify ? 1 : 0;

    if (!conn)
        return NULL;
    /* Connector/C 3.4 enables TLS + peer verification by default; honour the
     * caller's ssl_verify (default off) so plaintext servers can connect. */
    mysql_optionsv(conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
    /* On failure mysql_real_connect returns NULL but the MYSQL* stays valid for
     * mysql_error()/mysql_errno(); the caller tests errno and closes. */
    (void)mysql_real_connect(conn, host, user, password, db, port, NULL, 0);
    return conn;
}

int
UfsrvScriptMysqlQuery(MYSQL *conn, const char *sql, size_t sql_len)
{
    return mysql_real_query(conn, sql, (unsigned long)sql_len);
}

MYSQL_RES *
UfsrvScriptMysqlStoreResult(MYSQL *conn)
{
    return mysql_store_result(conn);
}

MYSQL_ROW
UfsrvScriptMysqlFetchRow(MYSQL_RES *res)
{
    return mysql_fetch_row(res);
}

unsigned long *
UfsrvScriptMysqlFetchLengths(MYSQL_RES *res)
{
    return mysql_fetch_lengths(res);
}

MYSQL_FIELD *
UfsrvScriptMysqlFetchFields(MYSQL_RES *res)
{
    return mysql_fetch_fields(res);
}

unsigned int
UfsrvScriptMysqlNumFields(MYSQL_RES *res)
{
    return mysql_num_fields(res);
}

unsigned long long
UfsrvScriptMysqlNumRows(MYSQL_RES *res)
{
    return (unsigned long long)mysql_num_rows(res);
}

unsigned int
UfsrvScriptMysqlFieldCount(MYSQL *conn)
{
    return mysql_field_count(conn);
}

unsigned long long
UfsrvScriptMysqlAffectedRows(MYSQL *conn)
{
    return (unsigned long long)mysql_affected_rows(conn);
}

unsigned long long
UfsrvScriptMysqlInsertId(MYSQL *conn)
{
    return (unsigned long long)mysql_insert_id(conn);
}

unsigned int
UfsrvScriptMysqlErrno(MYSQL *conn)
{
    return mysql_errno(conn);
}

const char *
UfsrvScriptMysqlError(MYSQL *conn)
{
    return mysql_error(conn);
}

char *
UfsrvScriptMysqlEscape(MYSQL *conn, const char *from, size_t from_len)
{
    char *to = (char *)malloc(2 * from_len + 1);

    if (!to)
        return NULL;
    (void)mysql_real_escape_string(conn, to, from, (unsigned long)from_len);
    return to;
}

int
UfsrvScriptMysqlPing(MYSQL *conn)
{
    return mysql_ping(conn);
}

int
UfsrvScriptMysqlCommit(MYSQL *conn)
{
    return (int)mysql_commit(conn);
}

int
UfsrvScriptMysqlRollback(MYSQL *conn)
{
    return (int)mysql_rollback(conn);
}

int
UfsrvScriptMysqlAutocommit(MYSQL *conn, int mode)
{
    return (int)mysql_autocommit(conn, (my_bool)mode);
}

void
UfsrvScriptMysqlFreeResult(MYSQL_RES *res)
{
    mysql_free_result(res);
}

void
UfsrvScriptMysqlClose(MYSQL *conn)
{
    mysql_close(conn);
}

/* ---------------------------------------------------------------------------
 * Lua object model.
 * ------------------------------------------------------------------------- */

typedef struct UfsrvScriptMysqlConn {
    MYSQL *handle;   /* NULL after close() */
} UfsrvScriptMysqlConn;

typedef struct UfsrvScriptMysqlResult {
    MYSQL       *conn;      /* borrowed raw handle (errno/error) */
    MYSQL_RES   *res;       /* NULL after close() */
    unsigned int numcols;
    int          conn_ref;  /* registry ref keeping the connection userdata alive */
} UfsrvScriptMysqlResult;

/* Return the connection's MYSQL*, raising if the userdata is closed. */
static MYSQL *
sCheckConnection(lua_State *lua_state_ptr, int idx)
{
    UfsrvScriptMysqlConn *conn = (UfsrvScriptMysqlConn *)luaL_checkudata(
        lua_state_ptr, idx, UFSRVSCRIPTLIB_MYSQL_CONN_MT);

    if (!conn->handle)
        luaL_error(lua_state_ptr, "ufsrvscriptlib_mysql: attempted to use a closed connection");
    return conn->handle;
}

/* Return the result, raising if it is closed. */
static UfsrvScriptMysqlResult *
sCheckResult(lua_State *lua_state_ptr, int idx)
{
    UfsrvScriptMysqlResult *res = (UfsrvScriptMysqlResult *)luaL_checkudata(
        lua_state_ptr, idx, UFSRVSCRIPTLIB_MYSQL_RESULT_MT);

    if (!res->res)
        luaL_error(lua_state_ptr, "ufsrvscriptlib_mysql: attempted to use a closed result");
    return res;
}

/* Push nil, errstr, errno (the module's multi-return error convention). */
static int
sPushError(lua_State *lua_state_ptr, MYSQL *conn)
{
    lua_pushnil(lua_state_ptr);
    lua_pushstring(lua_state_ptr, UfsrvScriptMysqlError(conn));
    lua_pushinteger(lua_state_ptr, (lua_Integer)UfsrvScriptMysqlErrno(conn));
    return 3;
}

/* Push one column cell, converted by its MYSQL_TYPE_* (type-aware, unlike
 * LuaSQL's all-strings). */
static void
sPushValue(lua_State *lua_state_ptr, const char *cell, unsigned long len,
           enum enum_field_types type)
{
    if (!cell) {
        lua_pushnil(lua_state_ptr);
        return;
    }

    switch (type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_YEAR:
            lua_pushinteger(lua_state_ptr, (lua_Integer)strtoll(cell, NULL, 10));
            break;

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            lua_pushnumber(lua_state_ptr, strtod(cell, NULL));
            break;

        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_BIT:
            lua_pushlstring(lua_state_ptr, cell, len);
            break;

        default: /* STRING, VARCHAR, DECIMAL, NEWDECIMAL, ENUM, SET, dates… */
            lua_pushlstring(lua_state_ptr, cell, len);
            break;
    }
}

/* Column type name (for result:columns()). */
static const char *
sColumnTypeName(enum enum_field_types type)
{
    switch (type) {
        case MYSQL_TYPE_TINY:      case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:      case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:     case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_FLOAT:     case MYSQL_TYPE_DOUBLE:
            return "number";
        case MYSQL_TYPE_NULL:
            return "null";
        case MYSQL_TYPE_TINY_BLOB: case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB: case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_BIT:
            return "binary";
        case MYSQL_TYPE_DATE:      case MYSQL_TYPE_NEWDATE:
            return "date";
        case MYSQL_TYPE_DATETIME:  case MYSQL_TYPE_DATETIME2:
            return "datetime";
        case MYSQL_TYPE_TIME:      case MYSQL_TYPE_TIME2:
            return "time";
        case MYSQL_TYPE_TIMESTAMP: case MYSQL_TYPE_TIMESTAMP2:
            return "timestamp";
        case MYSQL_TYPE_ENUM:      case MYSQL_TYPE_SET:
            return "set";
        default:
            return "string";
    }
}

/* ── result object ─────────────────────────────────────────────────────── */

static int sResultFetch(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlResult *res = sCheckResult(lua_state_ptr, 1);
    MYSQL_ROW row = UfsrvScriptMysqlFetchRow(res->res);
    MYSQL_FIELD *fields;
    unsigned long *lengths;
    unsigned int i;

    if (!row) {
        lua_pushnil(lua_state_ptr); /* end of result set */
        return 1;
    }

    fields = UfsrvScriptMysqlFetchFields(res->res);
    lengths = UfsrvScriptMysqlFetchLengths(res->res);

    lua_createtable(lua_state_ptr, 0, (int)res->numcols);
    for (i = 0; i < res->numcols; ++i) {
        lua_pushstring(lua_state_ptr, fields[i].name);
        sPushValue(lua_state_ptr, row[i], lengths[i], fields[i].type);
        lua_settable(lua_state_ptr, -3);
    }
    return 1;
}

static int sResultFetchAll(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlResult *res = sCheckResult(lua_state_ptr, 1);
    MYSQL_FIELD *fields = UfsrvScriptMysqlFetchFields(res->res);
    int i = 0;

    lua_newtable(lua_state_ptr);
    for (;;) {
        MYSQL_ROW row = UfsrvScriptMysqlFetchRow(res->res);
        unsigned long *lengths;
        unsigned int c;

        if (!row)
            break;
        lengths = UfsrvScriptMysqlFetchLengths(res->res);

        lua_createtable(lua_state_ptr, 0, (int)res->numcols);
        for (c = 0; c < res->numcols; ++c) {
            lua_pushstring(lua_state_ptr, fields[c].name);
            sPushValue(lua_state_ptr, row[c], lengths[c], fields[c].type);
            lua_settable(lua_state_ptr, -3);
        }
        lua_rawseti(lua_state_ptr, -2, ++i);
    }
    return 1;
}

static int sResultColumns(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlResult *res = sCheckResult(lua_state_ptr, 1);
    MYSQL_FIELD *fields = UfsrvScriptMysqlFetchFields(res->res);
    unsigned int i;

    lua_createtable(lua_state_ptr, (int)res->numcols, 0);
    for (i = 0; i < res->numcols; ++i) {
        lua_createtable(lua_state_ptr, 0, 2);
        lua_pushstring(lua_state_ptr, fields[i].name);
        lua_setfield(lua_state_ptr, -2, "name");
        lua_pushstring(lua_state_ptr, sColumnTypeName(fields[i].type));
        lua_setfield(lua_state_ptr, -2, "type");
        lua_rawseti(lua_state_ptr, -2, (int)i + 1);
    }
    return 1;
}

static int sResultNumRows(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlResult *res = sCheckResult(lua_state_ptr, 1);

    lua_pushinteger(lua_state_ptr, (lua_Integer)UfsrvScriptMysqlNumRows(res->res));
    return 1;
}

static int sResultClose(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlResult *res = (UfsrvScriptMysqlResult *)luaL_checkudata(
        lua_state_ptr, 1, UFSRVSCRIPTLIB_MYSQL_RESULT_MT);

    if (res && res->res) {
        UfsrvScriptMysqlFreeResult(res->res);
        res->res = NULL;
    }
    if (res && res->conn_ref != LUA_NOREF) {
        luaL_unref(lua_state_ptr, LUA_REGISTRYINDEX, res->conn_ref);
        res->conn_ref = LUA_NOREF;
    }
    return 0;
}

static int sResultToString(lua_State *lua_state_ptr)
{
    (void)sCheckResult(lua_state_ptr, 1);
    lua_pushliteral(lua_state_ptr, "ufsrvscript.mysql.result");
    return 1;
}

static const luaL_Reg sResultMt[] = {
    { "fetch",      sResultFetch },
    { "fetchall",   sResultFetchAll },
    { "columns",    sResultColumns },
    { "numrows",    sResultNumRows },
    { "close",      sResultClose },
    { "__gc",       sResultClose },
    { "__tostring", sResultToString },
    { NULL, NULL }
};

/* ── connection object ─────────────────────────────────────────────────── */

static int sConnQuery(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);
    size_t sql_len = 0;
    const char *sql = luaL_checklstring(lua_state_ptr, 2, &sql_len);
    MYSQL_RES *res;
    UfsrvScriptMysqlResult *result;

    if (UfsrvScriptMysqlQuery(conn, sql, sql_len))
        return sPushError(lua_state_ptr, conn);

    res = UfsrvScriptMysqlStoreResult(conn);
    if (res) {
        /* SELECT — a result set */
        result = (UfsrvScriptMysqlResult *)lua_newuserdata(lua_state_ptr, sizeof(*result));
        result->conn = conn;
        result->res = res;
        result->numcols = UfsrvScriptMysqlNumFields(res);
        lua_pushvalue(lua_state_ptr, 1); /* keep the connection alive via a ref */
        result->conn_ref = luaL_ref(lua_state_ptr, LUA_REGISTRYINDEX);

        if (luaL_newmetatable(lua_state_ptr, UFSRVSCRIPTLIB_MYSQL_RESULT_MT)) {
            luaL_setfuncs(lua_state_ptr, sResultMt, 0);
            lua_pushvalue(lua_state_ptr, -1);
            lua_setfield(lua_state_ptr, -2, "__index");
        }
        lua_setmetatable(lua_state_ptr, -2);
        return 1;
    }

    /* No result set: INSERT/UPDATE/DELETE (affected rows), or a SELECT whose
     * store_result failed (an error). */
    if (UfsrvScriptMysqlFieldCount(conn) == 0) {
        lua_pushinteger(lua_state_ptr, (lua_Integer)UfsrvScriptMysqlAffectedRows(conn));
        return 1;
    }
    return sPushError(lua_state_ptr, conn);
}

static int sConnEscape(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);
    size_t from_len = 0;
    const char *from = luaL_checklstring(lua_state_ptr, 2, &from_len);
    char *escaped = UfsrvScriptMysqlEscape(conn, from, from_len);

    if (!escaped)
        return luaL_error(lua_state_ptr, "ufsrvscriptlib_mysql: could not allocate escaped string");
    lua_pushstring(lua_state_ptr, escaped);
    free(escaped);
    return 1;
}

static int sConnCommit(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);

    lua_pushboolean(lua_state_ptr, UfsrvScriptMysqlCommit(conn) == 0);
    return 1;
}

static int sConnRollback(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);

    lua_pushboolean(lua_state_ptr, UfsrvScriptMysqlRollback(conn) == 0);
    return 1;
}

static int sConnSetAutocommit(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);
    int mode = lua_toboolean(lua_state_ptr, 2) ? 1 : 0;

    lua_pushboolean(lua_state_ptr, UfsrvScriptMysqlAutocommit(conn, mode) == 0);
    return 1;
}

static int sConnLastInsertId(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);

    lua_pushinteger(lua_state_ptr, (lua_Integer)UfsrvScriptMysqlInsertId(conn));
    return 1;
}

static int sConnAffectedRows(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);

    lua_pushinteger(lua_state_ptr, (lua_Integer)UfsrvScriptMysqlAffectedRows(conn));
    return 1;
}

static int sConnPing(lua_State *lua_state_ptr)
{
    MYSQL *conn = sCheckConnection(lua_state_ptr, 1);

    lua_pushboolean(lua_state_ptr, UfsrvScriptMysqlPing(conn) == 0);
    return 1;
}

static int sConnClose(lua_State *lua_state_ptr)
{
    UfsrvScriptMysqlConn *conn = (UfsrvScriptMysqlConn *)luaL_checkudata(
        lua_state_ptr, 1, UFSRVSCRIPTLIB_MYSQL_CONN_MT);

    if (conn && conn->handle) {
        UfsrvScriptMysqlClose(conn->handle);
        conn->handle = NULL;
    }
    return 0;
}

static int sConnToString(lua_State *lua_state_ptr)
{
    (void)sCheckConnection(lua_state_ptr, 1);
    lua_pushliteral(lua_state_ptr, "ufsrvscript.mysql.connection");
    return 1;
}

static const luaL_Reg sConnMt[] = {
    { "query",          sConnQuery },
    { "escape",         sConnEscape },
    { "commit",         sConnCommit },
    { "rollback",       sConnRollback },
    { "set_autocommit", sConnSetAutocommit },
    { "last_insert_id", sConnLastInsertId },
    { "affected_rows",  sConnAffectedRows },
    { "ping",           sConnPing },
    { "close",          sConnClose },
    { "__gc",           sConnClose },
    { "__tostring",     sConnToString },
    { NULL, NULL }
};

static int sConnect(lua_State *lua_state_ptr)
{
    const char *host = luaL_checkstring(lua_state_ptr, 1);
    int port = (int)luaL_checkinteger(lua_state_ptr, 2);
    const char *user = luaL_checkstring(lua_state_ptr, 3);
    const char *password = luaL_checkstring(lua_state_ptr, 4);
    const char *database = luaL_checkstring(lua_state_ptr, 5);
    int ssl_verify = lua_toboolean(lua_state_ptr, 6);  /* optional, default 0 */
    MYSQL *handle = UfsrvScriptMysqlConnect(host, user, password, database,
                                            (unsigned int)port, (unsigned int)ssl_verify);
    UfsrvScriptMysqlConn *conn;

    if (!handle) {
        lua_pushnil(lua_state_ptr);
        lua_pushliteral(lua_state_ptr, "mysql_init failed (out of memory)");
        return 2;
    }
    if (UfsrvScriptMysqlErrno(handle) != 0) {
        int nret = sPushError(lua_state_ptr, handle);

        UfsrvScriptMysqlClose(handle);
        return nret;
    }

    conn = (UfsrvScriptMysqlConn *)lua_newuserdata(lua_state_ptr, sizeof(*conn));
    conn->handle = handle;

    if (luaL_newmetatable(lua_state_ptr, UFSRVSCRIPTLIB_MYSQL_CONN_MT)) {
        luaL_setfuncs(lua_state_ptr, sConnMt, 0);
        lua_pushvalue(lua_state_ptr, -1);
        lua_setfield(lua_state_ptr, -2, "__index");
    }
    lua_setmetatable(lua_state_ptr, -2);

    return 1;
}

static const luaL_Reg sMysqlModule[] = {
    { "connect", sConnect },
    { NULL, NULL }
};

PUBLIC_API void
UfsrvScriptMysqlRegister(lua_State *lua_state_ptr)
{
    lua_newtable(lua_state_ptr); /* the mysql module table */

    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_MYSQL_VERSION);
    lua_setfield(lua_state_ptr, -2, "_VERSION");
    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_MYSQL_COPYRIGHT);
    lua_setfield(lua_state_ptr, -2, "_COPYRIGHT");
    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_MYSQL_DESCRIPTION);
    lua_setfield(lua_state_ptr, -2, "_DESCRIPTION");
    lua_pushstring(lua_state_ptr, mysql_get_client_info());
    lua_setfield(lua_state_ptr, -2, "_CLIENTVERSION");

    luaL_setfuncs(lua_state_ptr, sMysqlModule, 0);

    lua_setglobal(lua_state_ptr, "mysql");
}
