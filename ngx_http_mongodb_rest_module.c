/*
 * Copyright 2012 Alex Chamberlain
 * Some portions Copyright 2009-2010 Michael Dirolf

 * Dual Licensed under the Apache License, Version 2.0 and the GNU
 * General Public License, version 2 or (at your option) any later
 * version.
 *
 * -- Apache License
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * -- GNU GPL
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * TODO range support http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.35
 */

/* Tuning Parameters */
#define MONGO_MAX_RETRIES_PER_REQUEST 1
#define MONGO_RECONNECT_WAITTIME 500 //ms

#define TRUE 1
#define FALSE 0


/* Nginx Includes */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* Standard Includes */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

/* Mongo Includes - link with -lmongo -lbson */
#include <mongodb-c/mongo.h>
#include <mongodb-c/gridfs.h>

#include "jsonbson.h"

/**
 * Types
 */

/* Main Configuration */
typedef struct {
    ngx_array_t loc_confs; /* ngx_http_mongodb_rest_loc_conf_t */
} ngx_http_mongodb_rest_main_conf_t;

/* Location Configuration */
typedef struct {
    ngx_str_t db;
    ngx_str_t root_collection;
    ngx_str_t field;
    ngx_uint_t type;
    ngx_str_t user;
    ngx_str_t pass;
    ngx_str_t mongo;
    ngx_array_t* mongods; /* ngx_http_mongod_server_t */
    ngx_str_t replset; /* Name of the replica set, if connecting. */
} ngx_http_mongodb_rest_loc_conf_t;

/* Mongo Authentication Credentials */
typedef struct {
    ngx_str_t db;
    ngx_str_t user;
    ngx_str_t pass;
} ngx_http_mongo_auth_t;

/* Persistent (to process) MongoDB Connections */
typedef struct {
    ngx_str_t name;
    mongo conn;
    ngx_array_t *auths; /* ngx_http_mongo_auth_t */
} ngx_http_mongo_connection_t;


// Maybe we should store a list of addresses instead.
typedef struct {
    ngx_str_t host;
    in_port_t port;
} ngx_http_mongod_server_t;

typedef struct {
    mongo_cursor ** cursors;
    ngx_uint_t numchunks;
} ngx_http_mongodb_rest_cleanup_t;

/**
 * Public Interface
 */

/* Module definition. */
// Forward definitions - variables
static ngx_http_module_t ngx_http_mongodb_rest_module_ctx;
static ngx_command_t ngx_http_mongodb_rest_commands[];

// Forward definitions - functions
static ngx_int_t ngx_http_mongodb_rest_init_worker(ngx_cycle_t* cycle);

ngx_module_t ngx_http_mongodb_rest_module = {
    NGX_MODULE_V1,
    &ngx_http_mongodb_rest_module_ctx,
    ngx_http_mongodb_rest_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    ngx_http_mongodb_rest_init_worker,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

/* Module context. */
// Forward declarations - functions
static void* ngx_http_mongodb_rest_create_main_conf(ngx_conf_t* directive);
static void* ngx_http_mongodb_rest_create_loc_conf(ngx_conf_t* directive);
static char* ngx_http_mongodb_rest_merge_loc_conf(ngx_conf_t* directive, void* parent, void* child);

static ngx_http_module_t ngx_http_mongodb_rest_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */
    ngx_http_mongodb_rest_create_main_conf,
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* init server configuration */
    ngx_http_mongodb_rest_create_loc_conf,
    ngx_http_mongodb_rest_merge_loc_conf
};

/* Array specifying how to handle configuration directives. */
// Forward declarations - functions
static char * ngx_http_mongo(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);
static char* ngx_http_mongodb_rest(ngx_conf_t* directive, ngx_command_t* command, void* mongodb_rest_conf);

static ngx_command_t ngx_http_mongodb_rest_commands[] = {
    {
        ngx_string("mongo"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_http_mongo,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("mongodb-rest"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_http_mongodb_rest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};


static ngx_int_t ngx_http_mongodb_rest_handler(ngx_http_request_t* request);
//static void ngx_http_mongodb_rest_cleanup(void* data);

static ngx_array_t ngx_http_mongo_connections;

static ngx_http_mongo_connection_t* ngx_http_get_mongo_connection( ngx_str_t name ) {
    ngx_http_mongo_connection_t *mongo_conns;
    ngx_uint_t i;

    mongo_conns = ngx_http_mongo_connections.elts;

    for ( i = 0; i < ngx_http_mongo_connections.nelts; i++ ) {
        if ( name.len == mongo_conns[i].name.len
             && ngx_strncmp(name.data, mongo_conns[i].name.data, name.len) == 0 ) {
            return &mongo_conns[i];
        }
    }

    return NULL;
}

static ngx_int_t ngx_http_mongo_authenticate(ngx_log_t *log, ngx_http_mongodb_rest_loc_conf_t *mongodb_rest_loc_conf) {
    ngx_http_mongo_connection_t* mongo_conn;
    ngx_http_mongo_auth_t *mongo_auth;
    mongo_cursor *cursor = NULL;
    bson empty;
    char *test;
    int error;

    mongo_conn = ngx_http_get_mongo_connection( mongodb_rest_loc_conf->mongo );
    if (mongo_conn == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                  "Mongo Connection not found: \"%V\"", &mongodb_rest_loc_conf->mongo);
    }

    // Authenticate
    if (mongodb_rest_loc_conf->user.data != NULL && mongodb_rest_loc_conf->pass.data != NULL) {
        if (!mongo_cmd_authenticate( &mongo_conn->conn, 
                                     (const char*)mongodb_rest_loc_conf->db.data, 
                                     (const char*)mongodb_rest_loc_conf->user.data, 
                                     (const char*)mongodb_rest_loc_conf->pass.data )) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Invalid mongo user/pass: %s/%s", 
                          mongodb_rest_loc_conf->user.data, 
                          mongodb_rest_loc_conf->pass.data);
            return NGX_ERROR;
        }

        mongo_auth = ngx_array_push(mongo_conn->auths);
        mongo_auth->db = mongodb_rest_loc_conf->db;
        mongo_auth->user = mongodb_rest_loc_conf->user;
        mongo_auth->pass = mongodb_rest_loc_conf->pass;
    }

    // Run a test command to test authentication.
    test = (char*)malloc( mongodb_rest_loc_conf->db.len + sizeof(".test"));
    ngx_cpystrn((u_char*)test, (u_char*)mongodb_rest_loc_conf->db.data, mongodb_rest_loc_conf->db.len+1);
    ngx_cpystrn((u_char*)(test+mongodb_rest_loc_conf->db.len),(u_char*)".test", sizeof(".test"));
    bson_empty(&empty);
    cursor = mongo_find(&mongo_conn->conn, test, &empty, NULL, 0, 0, 0);
    error =  mongo_cmd_get_last_error(&mongo_conn->conn, (char*)mongodb_rest_loc_conf->db.data, NULL);
    free(test);
    mongo_cursor_destroy(cursor);
    if (error) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "Authentication Required");
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_mongo_add_connection(ngx_cycle_t* cycle, ngx_http_mongodb_rest_loc_conf_t* mongodb_rest_loc_conf) {
    ngx_http_mongo_connection_t* mongo_conn;
    int status;
    ngx_http_mongod_server_t *mongods;
    volatile ngx_uint_t i;
    u_char host[255];

    mongods = mongodb_rest_loc_conf->mongods->elts;

    mongo_conn = ngx_http_get_mongo_connection( mongodb_rest_loc_conf->mongo );
    if (mongo_conn != NULL) {
        return NGX_OK;
    }

    mongo_conn = ngx_array_push(&ngx_http_mongo_connections);
    if (mongo_conn == NULL) {
        return NGX_ERROR;
    }

    mongo_conn->name = mongodb_rest_loc_conf->mongo;
    mongo_conn->auths = ngx_array_create(cycle->pool, 4, sizeof(ngx_http_mongo_auth_t));

    if ( mongodb_rest_loc_conf->mongods->nelts == 1 ) {
        ngx_cpystrn( host, mongods[0].host.data, mongods[0].host.len + 1 );
        status = mongo_connect( &mongo_conn->conn, (const char*)host, mongods[0].port );
    } else if ( mongodb_rest_loc_conf->mongods->nelts >= 2 && mongodb_rest_loc_conf->mongods->nelts < 9 ) {

        /* Initiate replica set connection. */
        mongo_replset_init( &mongo_conn->conn, (const char *)mongodb_rest_loc_conf->replset.data );

        /* Add replica set seeds. */
        for( i=0; i<mongodb_rest_loc_conf->mongods->nelts; ++i ) {
            ngx_cpystrn( host, mongods[i].host.data, mongods[i].host.len + 1 );
            mongo_replset_add_seed( &mongo_conn->conn, (const char *)host, mongods[i].port );
        }
        status = mongo_replset_connect( &mongo_conn->conn );
    } else {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Nginx Exception: Too many strings provided in 'mongo' directive.");
        return NGX_ERROR;
    }

    switch (status) {
        case MONGO_CONN_SUCCESS:
            break;
        case MONGO_CONN_NO_SOCKET:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: No Socket");
            return NGX_ERROR;
        case MONGO_CONN_FAIL:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Connection Failure.");
            return NGX_ERROR;
        case MONGO_CONN_ADDR_FAIL:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: getaddrinfo Failure.");
            return NGX_ERROR;
        case MONGO_CONN_NOT_MASTER:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Not Master");
            return NGX_ERROR;
        case MONGO_CONN_BAD_SET_NAME:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Replica set name %s does not match.", mongodb_rest_loc_conf->replset.data);
            return NGX_ERROR;
        case MONGO_CONN_NO_PRIMARY:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Cannot connect to primary node.");
            return NGX_ERROR;
        default:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Unknown Error");
            return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_mongodb_rest_init_worker(ngx_cycle_t* cycle) {
    ngx_http_mongodb_rest_main_conf_t* mongodb_rest_main_conf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_mongodb_rest_module);
    ngx_http_mongodb_rest_loc_conf_t** mongodb_rest_loc_confs;
    ngx_uint_t i;

    signal(SIGPIPE, SIG_IGN);

    mongodb_rest_loc_confs = mongodb_rest_main_conf->loc_confs.elts;

    ngx_array_init(&ngx_http_mongo_connections, cycle->pool, 4, sizeof(ngx_http_mongo_connection_t));

    for (i = 0; i < mongodb_rest_main_conf->loc_confs.nelts; i++) {
        if (ngx_http_mongo_add_connection(cycle, mongodb_rest_loc_confs[i]) == NGX_ERROR) {
            return NGX_ERROR;
        }
        if (ngx_http_mongo_authenticate(cycle->log, mongodb_rest_loc_confs[i]) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

/* Parse the 'mongo' directive. */
static char * ngx_http_mongo(ngx_conf_t *cf, ngx_command_t *cmd, void *void_conf) {
    ngx_str_t *value;
    ngx_url_t u;
    ngx_uint_t i;
    ngx_uint_t start;
    ngx_http_mongod_server_t *mongod_server;
    ngx_http_mongodb_rest_loc_conf_t *mongodb_rest_loc_conf;

    mongodb_rest_loc_conf = void_conf;

    value = cf->args->elts;
    mongodb_rest_loc_conf->mongo = value[1];
    mongodb_rest_loc_conf->mongods = ngx_array_create(cf->pool, 7,
                                                sizeof(ngx_http_mongod_server_t));
    if (mongodb_rest_loc_conf->mongods == NULL) {
        return NULL;
    }

    /* If nelts is greater than 3, then the user has specified more than one
     * setting in the 'mongo' directive. So we assume that we're connecting
     * to a replica set and that the first string of the directive is the replica
     * set name. We also start looking for host-port pairs at position 2; otherwise,
     * we start at position 1.
     */
    if( cf->args->nelts >= 3 ) {
        mongodb_rest_loc_conf->replset.len = strlen( (char *)(value + 1)->data );
        mongodb_rest_loc_conf->replset.data = ngx_pstrdup( cf->pool, value + 1 );
        start = 2;
    } else
        start = 1;

    for (i = start; i < cf->args->nelts; i++) {

        ngx_memzero(&u, sizeof(ngx_url_t));

        u.url = value[i];
        u.default_port = 27017;

        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in mongo \"%V\"", u.err, &u.url);
            }
            return NGX_CONF_ERROR;
        }

        mongod_server = ngx_array_push(mongodb_rest_loc_conf->mongods);
        mongod_server->host = u.host;
        mongod_server->port = u.port;

    }

    return NGX_CONF_OK;
}

/* Parse the 'mongodb-rest' directive. */
static char* ngx_http_mongodb_rest(ngx_conf_t* cf, ngx_command_t* command, void* void_conf) {
    ngx_http_mongodb_rest_loc_conf_t *mongodb_rest_loc_conf = void_conf;
    ngx_http_core_loc_conf_t* core_conf;
    ngx_str_t *value, type;
    volatile ngx_uint_t i;

    core_conf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    core_conf-> handler = ngx_http_mongodb_rest_handler;

    value = cf->args->elts;
    mongodb_rest_loc_conf->db = value[1];

    /* Parse the parameters */
    for (i = 2; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "root_collection=", 16) == 0) { 
            mongodb_rest_loc_conf->root_collection.data = (u_char *) &value[i].data[16];
            mongodb_rest_loc_conf->root_collection.len = ngx_strlen(&value[i].data[16]);
            continue;
        }

        if (ngx_strncmp(value[i].data, "field=", 6) == 0) {
            mongodb_rest_loc_conf->field.data = (u_char *) &value[i].data[6];
            mongodb_rest_loc_conf->field.len = ngx_strlen(&value[i].data[6]);

            /* Currently only support for "_id" and "filename" */
            if (mongodb_rest_loc_conf->field.data != NULL
                && ngx_strcmp(mongodb_rest_loc_conf->field.data, "filename") != 0
                && ngx_strcmp(mongodb_rest_loc_conf->field.data, "_id") != 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "Unsupported Field: %s", mongodb_rest_loc_conf->field.data);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "type=", 5) == 0) { 
            type = (ngx_str_t) ngx_string(&value[i].data[5]);

            /* Currently only support for "objectid", "string", and "int" */
            if (type.len == 0) {
                mongodb_rest_loc_conf->type = NGX_CONF_UNSET_UINT;
            } else if (ngx_strcasecmp(type.data, (u_char *)"objectid") == 0) {
                mongodb_rest_loc_conf->type = BSON_OID;
            } else if (ngx_strcasecmp(type.data, (u_char *)"string") == 0) {
                mongodb_rest_loc_conf->type = BSON_STRING;
            } else if (ngx_strcasecmp(type.data, (u_char *)"int") == 0) {
                mongodb_rest_loc_conf->type = BSON_INT;
            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "Unsupported Type: %s", (char *)value[i].data);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "user=", 5) == 0) { 
            mongodb_rest_loc_conf->user.data = (u_char *) &value[i].data[5];
            mongodb_rest_loc_conf->user.len = ngx_strlen(&value[i].data[5]);
            continue;
        }

        if (ngx_strncmp(value[i].data, "pass=", 5) == 0) {
            mongodb_rest_loc_conf->pass.data = (u_char *) &value[i].data[5];
            mongodb_rest_loc_conf->pass.len = ngx_strlen(&value[i].data[5]);
            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (mongodb_rest_loc_conf->field.data != NULL
        && ngx_strcmp(mongodb_rest_loc_conf->field.data, "filename") == 0
        && mongodb_rest_loc_conf->type != BSON_STRING) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Field: filename, must be of Type: string");
        return NGX_CONF_ERROR;
    }

    if ((mongodb_rest_loc_conf->user.data == NULL || mongodb_rest_loc_conf->user.len == 0)
        && !(mongodb_rest_loc_conf->pass.data == NULL || mongodb_rest_loc_conf->pass.len == 0)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Password without username");
        return NGX_CONF_ERROR;
    }

    if (!(mongodb_rest_loc_conf->user.data == NULL || mongodb_rest_loc_conf->user.len == 0)
        && (mongodb_rest_loc_conf->pass.data == NULL || mongodb_rest_loc_conf->pass.len == 0)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Username without password");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static void *ngx_http_mongodb_rest_create_main_conf(ngx_conf_t *cf) {
    ngx_http_mongodb_rest_main_conf_t  *mongodb_rest_main_conf;

    mongodb_rest_main_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mongodb_rest_main_conf_t));
    if (mongodb_rest_main_conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&mongodb_rest_main_conf->loc_confs, cf->pool, 4,
                       sizeof(ngx_http_mongodb_rest_loc_conf_t *))
        != NGX_OK) {
        return NULL;
    }

    return mongodb_rest_main_conf;
}

static void* ngx_http_mongodb_rest_create_loc_conf(ngx_conf_t* directive) {
    ngx_http_mongodb_rest_loc_conf_t* mongodb_rest_conf;

    mongodb_rest_conf = ngx_pcalloc(directive->pool, sizeof(ngx_http_mongodb_rest_loc_conf_t));
    if (mongodb_rest_conf == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Failed to allocate memory for GridFS Location Config.");
        return NGX_CONF_ERROR;
    }

    mongodb_rest_conf->db.data = NULL;
    mongodb_rest_conf->db.len = 0;
    mongodb_rest_conf->root_collection.data = NULL;
    mongodb_rest_conf->root_collection.len = 0;
    mongodb_rest_conf->field.data = NULL;
    mongodb_rest_conf->field.len = 0;
    mongodb_rest_conf->type = NGX_CONF_UNSET_UINT;
    mongodb_rest_conf->user.data = NULL;
    mongodb_rest_conf->user.len = 0;
    mongodb_rest_conf->pass.data = NULL;
    mongodb_rest_conf->pass.len = 0;
    mongodb_rest_conf->mongo.data = NULL;
    mongodb_rest_conf->mongo.len = 0;
    mongodb_rest_conf->mongods = NGX_CONF_UNSET_PTR;

    return mongodb_rest_conf;
}

static char* ngx_http_mongodb_rest_merge_loc_conf(ngx_conf_t* cf, void* void_parent, void* void_child) {
    ngx_http_mongodb_rest_loc_conf_t *parent = void_parent;
    ngx_http_mongodb_rest_loc_conf_t *child = void_child;
    ngx_http_mongodb_rest_main_conf_t *mongodb_rest_main_conf = ngx_http_conf_get_module_main_conf(cf, ngx_http_mongodb_rest_module);
    ngx_http_mongodb_rest_loc_conf_t **mongodb_rest_loc_conf;
    ngx_http_mongod_server_t *mongod_server;

    ngx_conf_merge_str_value(child->db, parent->db, NULL);
    ngx_conf_merge_str_value(child->root_collection, parent->root_collection, "fs");
    ngx_conf_merge_str_value(child->field, parent->field, "_id");
    ngx_conf_merge_uint_value(child->type, parent->type, BSON_OID);
    ngx_conf_merge_str_value(child->user, parent->user, NULL);
    ngx_conf_merge_str_value(child->pass, parent->pass, NULL);
    ngx_conf_merge_str_value(child->mongo, parent->mongo, "127.0.0.1:27017");

    if (child->mongods == NGX_CONF_UNSET_PTR) {
        if (parent->mongods != NGX_CONF_UNSET_PTR) {
            child->mongods = parent->mongods;
        } else {
            child->mongods = ngx_array_create(cf->pool, 4,
                                              sizeof(ngx_http_mongod_server_t));
            mongod_server = ngx_array_push(child->mongods);
            mongod_server->host.data = (u_char *)"127.0.0.1";
            mongod_server->host.len = sizeof("127.0.0.1") - 1;
            mongod_server->port = 27017;
        }
    }

    // Add the local mongodb_rest conf to the main mongodb_rest conf
    if (child->db.data) {
        mongodb_rest_loc_conf = ngx_array_push(&mongodb_rest_main_conf->loc_confs);
        *mongodb_rest_loc_conf = child;
    }

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_mongo_reconnect(ngx_log_t *log, ngx_http_mongo_connection_t *mongo_conn) {
    volatile int status = MONGO_CONN_FAIL;

    if (&mongo_conn->conn.connected) { 
        mongo_disconnect(&mongo_conn->conn);
        ngx_msleep(MONGO_RECONNECT_WAITTIME);
        status = mongo_reconnect(&mongo_conn->conn);
    } else {
        status = MONGO_CONN_FAIL;
    }

    switch (status) {
        case MONGO_CONN_SUCCESS:
            break;
        case MONGO_CONN_NO_SOCKET:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: No Socket");
            return NGX_ERROR;
        case MONGO_CONN_FAIL:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Connection Failure %s:%i;",
                          mongo_conn->conn.primary->host,
                          mongo_conn->conn.primary->port);
            return NGX_ERROR;
        case MONGO_CONN_ADDR_FAIL:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: getaddrinfo Failure");
            return NGX_ERROR;
        case MONGO_CONN_NOT_MASTER:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Not Master");
            return NGX_ERROR;
        default:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Unknown Error");
            return NGX_ERROR;
    }
    
    return NGX_OK;
}

static ngx_int_t ngx_http_mongo_reauth(ngx_log_t *log, ngx_http_mongo_connection_t *mongo_conn) {
    ngx_http_mongo_auth_t *auths;
    volatile ngx_uint_t i, success = 0;
    auths = mongo_conn->auths->elts;

    for (i = 0; i < mongo_conn->auths->nelts; i++) {
        success = mongo_cmd_authenticate( &mongo_conn->conn, 
                                          (const char*)auths[i].db.data, 
                                          (const char*)auths[i].user.data, 
                                          (const char*)auths[i].pass.data );
        if (!success) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Invalid mongo user/pass: %s/%s, during reauth", 
                          auths[i].user.data, 
                          auths[i].pass.data);   
            return NGX_ERROR;
        }
    }
    
    return NGX_OK;
}

static char h_digit(char hex) {
    return (hex >= '0' && hex <= '9') ? hex - '0': ngx_tolower(hex)-'a'+10;
}

static int htoi(char* h) {
    char ok[] = "0123456789AaBbCcDdEeFf";

    if (ngx_strchr(ok, h[0]) == NULL || ngx_strchr(ok,h[1]) == NULL) { return -1; }
    return h_digit(h[0])*16 + h_digit(h[1]);
}

static int url_decode(char * filename) {
    char * read = filename;
    char * write = filename;
    char hex[3];
    int c;

    hex[2] = '\0';
    while (*read != '\0'){
        if (*read == '%') {
            hex[0] = *(++read);
            if (hex[0] == '\0') return 0;
            hex[1] = *(++read);
            if (hex[1] == '\0') return 0;
            c = htoi(hex);
            if (c == -1) return 0;
            *write = (char)c;
        }
        else *write = *read;
        read++;
        write++;
    }
    *write = '\0';
    return 1;
}

static unsigned char ngx_http_mongodb_rest_query_init(bson * query, bson_type type, const char * field, const char * value) {
  bson_oid_t oid;

  bson_init(query);
  switch (type) {
    case  BSON_OID:
      bson_oid_from_string(&oid, value);
      bson_append_oid(query, field, &oid);
      break;
    case BSON_INT:
      bson_append_int(query, field, ngx_atoi((u_char*)value, strlen(value)));
      break;
    case BSON_STRING:
      bson_append_string(query, field, value);
      break;
    default:
      return 0;
      break;
  }
  bson_finish(query);

  /*int ql = json_length(&query);
  char * qs = (char *) ngx_palloc(request->pool, ql);
  tojson(&query, qs);
  ngx_log_error(NGX_LOG_DEBUG, request->connection->log,0, qs);*/

  return 1;
}

static ngx_int_t ngx_http_mongodb_rest_get_handler(ngx_http_request_t* request, mongo * conn, bson_type type, const char * field, char * collection, const char * value) {
  ngx_buf_t* buffer;
  ngx_chain_t out;

  bson query;
  mongo_cursor cursor;

  const bson * b;
  int l;
  char * s;

  ngx_int_t rc;

  if(!ngx_http_mongodb_rest_query_init(&query, type, field, value)) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  // ---------- RETRIEVE OBJECT ---------- //
  mongo_cursor_init(&cursor, conn, "test.test");
  mongo_cursor_set_query(&cursor, &query);

  if(mongo_cursor_next(&cursor) != MONGO_OK) {
    return NGX_HTTP_NOT_FOUND;
  }

  b = mongo_cursor_bson(&cursor);
  l = json_length(b);
  s = (char *) ngx_palloc(request->pool, l);
  
  if(s == NULL)
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  
  tojson(b, s);

  ngx_log_error(NGX_LOG_DEBUG, request->connection->log,0, "JSON: %s (%d/%d)", s, strlen(s), l-1);

  mongo_cursor_destroy(&cursor);
  bson_destroy(&query);

  // ---------- SEND THE HEADERS ---------- //

  request->headers_out.status = NGX_HTTP_OK;
  request->headers_out.content_length_n = l -1;
  ngx_str_set(&request->headers_out.content_type, "text/json");

  ngx_http_send_header(request);

  // ---------- SEND THE BODY ---------- //

  /* Allocate space for the response buffer */
  buffer = ngx_pcalloc(request->pool, sizeof(ngx_buf_t));
  if (buffer == NULL) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
		  "Failed to allocate response buffer");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Set up the buffer chain */
  buffer->pos = (u_char*)s;
  buffer->last = (u_char*)s + l - 1; // Don't write NULL
  buffer->memory = 1;
  buffer->last_buf = 1;
  out.buf = buffer;
  out.next = NULL;

  /* Serve the Chunk */
  rc = ngx_http_output_filter(request, &out);

  /* TODO: Do we need to do anything on error? */
  return rc;
}

static ngx_int_t ngx_http_mongodb_rest_delete_handler(ngx_http_request_t* request, mongo * conn, bson_type type, const char * field, char * collection, const char * value) {
  bson query;
  mongo_cursor cursor;

  if(!ngx_http_mongodb_rest_query_init(&query, type, field, value)) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  // ---------- RETRIEVE OBJECT ---------- //
  mongo_cursor_init(&cursor, conn, "test.test");
  mongo_cursor_set_query(&cursor, &query);

  if(mongo_cursor_next(&cursor) != MONGO_OK) {
    return NGX_HTTP_NOT_FOUND;
  }

  mongo_cursor_destroy(&cursor);
  
  if(mongo_remove(conn, "test.test", &query) != MONGO_OK) {
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  } else {
    request->headers_out.status = NGX_HTTP_NO_CONTENT;
    ngx_http_send_header(request);

    return NGX_OK;
  }
}

static void ngx_http_mongodb_rest_put_read(ngx_http_request_t* r) {
  u_char * p;
  size_t len;

  ngx_buf_t *buf, *next;
  ngx_chain_t *cl;

  ngx_buf_t *buffer;
  ngx_chain_t out;

  if(r->request_body == NULL
    || r->request_body->bufs == NULL
    || r->request_body->temp_file) {
    return;
  }

  cl = r->request_body->bufs;
  buf = cl->buf;

  if(cl->next == NULL) {
    p = buf->pos;
    len = buf->last - buf->pos;
  } else {
    /* POST request did not fit into a single buffer, there must be 2. */
    u_char * q;

    next = cl->next->buf;
    len = (buf->last - buf->pos) + (next->last - next->pos);

    p = ngx_pnalloc(r->pool, len);
    if(p == NULL) {
      ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
      return;
    }

    q = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    ngx_memcpy(q, next->pos, next->last - next->pos);
  }

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "%*s", len, p);

  r->headers_out.status = NGX_HTTP_NO_CONTENT;
  ngx_http_send_header(r);

  ngx_http_finalize_request(r, NGX_HTTP_NO_CONTENT);
}

static ngx_int_t ngx_http_mongodb_rest_put_handler(ngx_http_request_t* r, mongo * conn, bson_type type, const char * field, char * collection, const char * value) {
  ngx_int_t rc;

  rc = ngx_http_read_client_request_body(r, ngx_http_mongodb_rest_put_read);

  if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
    return rc;
  }

  return NGX_DONE;
}

static ngx_int_t ngx_http_mongodb_rest_handler(ngx_http_request_t* request) {
    ngx_http_mongodb_rest_loc_conf_t* mongodb_rest_conf;
    ngx_http_core_loc_conf_t* core_conf;
    ngx_str_t location_name;
    ngx_str_t full_uri;
    char* value;
    ngx_http_mongo_connection_t *mongo_conn;

    ngx_int_t rc = NGX_OK;

    mongodb_rest_conf = ngx_http_get_module_loc_conf(request, ngx_http_mongodb_rest_module);
    core_conf = ngx_http_get_module_loc_conf(request, ngx_http_core_module);

    // ---------- ENSURE MONGO CONNECTION ---------- //

    mongo_conn = ngx_http_get_mongo_connection( mongodb_rest_conf->mongo );
    if (mongo_conn == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Mongo Connection not found: \"%V\"", &mongodb_rest_conf->mongo);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    if ( !(&mongo_conn->conn.connected)
         && (ngx_http_mongo_reconnect(request->connection->log, mongo_conn) == NGX_ERROR
             || ngx_http_mongo_reauth(request->connection->log, mongo_conn) == NGX_ERROR)) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Could not connect to mongo: \"%V\"", &mongodb_rest_conf->mongo);
        if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    // ---------- RETRIEVE KEY ---------- //

    location_name = core_conf->name;
    full_uri = request->uri;

    if (full_uri.len < location_name.len) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Invalid location name or uri.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    value = (char*)malloc(sizeof(char) * (full_uri.len - location_name.len + 1));
    if (value == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Failed to allocate memory for value buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    memcpy(value, full_uri.data + location_name.len, full_uri.len - location_name.len);
    value[full_uri.len - location_name.len] = '\0';

    if (!url_decode(value)) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Malformed request.");
        free(value);
        return NGX_HTTP_BAD_REQUEST;
    }

    unsigned char* m = request->method_name.data;
    size_t ml = request->method_name.len;

    switch(ml) {
      case 3:
        if(m[0] == 'G'
	  && m[1] == 'E'
	  && m[2] == 'T') {
          rc = ngx_http_mongodb_rest_get_handler(request, &mongo_conn->conn, mongodb_rest_conf->type, (char*) mongodb_rest_conf->field.data, "test", value);
	} else if(m[0] == 'P'
	  && m[1] == 'U'
	  && m[2] == 'T') {
	  rc = ngx_http_mongodb_rest_put_handler(request, &mongo_conn->conn, mongodb_rest_conf->type, (char*) mongodb_rest_conf->field.data, "test", value);
	} else {
	  rc = NGX_HTTP_NOT_ALLOWED;
	}
	break;
      case 4:
	if(m[0] == 'P'
	  && m[1] == 'O'
	  && m[2] == 'S'
	  && m[3] == 'T') {
	  rc = NGX_HTTP_NOT_ALLOWED;
	} else {
	  rc = NGX_HTTP_NOT_ALLOWED;
	}
	break;
      case 6:
	if(m[0] == 'D'
	  && m[1] == 'E'
	  && m[2] == 'L'
	  && m[3] == 'E'
	  && m[4] == 'T'
	  && m[5] == 'E') {
	  rc = ngx_http_mongodb_rest_delete_handler(request, &mongo_conn->conn, mongodb_rest_conf->type, (char*) mongodb_rest_conf->field.data, "test", value);
	} else {
	  rc = NGX_HTTP_NOT_ALLOWED;
	}
        break;
      default:
        rc = NGX_HTTP_NOT_ALLOWED;
    }

    free(value);
    return rc;
}
