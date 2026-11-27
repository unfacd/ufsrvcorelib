/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file server_registry_redis_defs.h
 * @brief Redis command formats for the ufsrv server registry.
 */
#ifndef UFSRVCORELIB_SERVER_REGISTRY_REDIS_DEFS_H
#define UFSRVCORELIB_SERVER_REGISTRY_REDIS_DEFS_H

//server_class_<geogroup>
#define REDIS_CMD_CONFIG_UFSRV_REQID_INC			"HINCRBY _CONFIG_UFSRV_%s_%d reqid 1"
#define REDIS_CMD_CONFIG_UFSRV_REQID_GET			"HMGET _CONFIG_UFSRV_%s_%d reqid"
#define REDIS_CMD_CONFIG_UFSRV_GROUPSZ_GET		"HMGET _CONFIG_UFSRV_%s_%d group_sz"
 //<server class> <%server geo group> <server id>
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_ADD			"SADD _CONFIG_UFSRV_MEMBERS_%s_%d %d"
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_GETALL	"SMEMBERS _CONFIG_UFSRV_MEMBERS_%s_%d" //<%class>_<%geogroup> : 'ufsrv_3'
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_REM			"SREM _CONFIG_UFSRV_MEMBERS_%s:%d %d"
#define REDIS_CMD_CONFIG_UFSRV_MEMBERS_SZ			"SCARD _CONFIG_UFSRV_MEMBERS_%s:%d"

 //individual attributes for a given server
 //<server class> <%server geo group> <server id> <last active time>
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTR_LAST_SET			"HMSET _CONFIG_UFSRV_MEMBER_%s_%d_%d last %lu"
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTR_LAST_GET			"HMGET _CONFIG_UFSRV_MEMBER_%s_%d_%d last"
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTRS_GETALL			"HGETALL _CONFIG_UFSRV_MEMBER_%s_%d_%d"
#define REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTRS_IDENTIFIERS_SET		"HMSET _CONFIG_UFSRV_MEMBER_%s_%d_%d pid %i ip %s started %lu serverid %i last %lu"

#endif /* UFSRVCORELIB_SERVER_REGISTRY_REDIS_DEFS_H */
