/*
  +----------------------------------------------------------------------+
  | PHP Version 5.3                                                      |
  +----------------------------------------------------------------------+
  | Copyright (c) 2002-2011 Dmitri Vinogradov                            |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Dmitri Vinogradov <dmitri.vinogradov@gmail.com>             |
  +----------------------------------------------------------------------+
*/

#ifndef BUDDEL_HTTPSVC_SERVER_H
#define BUDDEL_HTTPSVC_SERVER_H

#include "php.h"
#include "fopen_wrappers.h"
#include "ext/date/php_date.h"
#include "ext/standard/php_array.h"
#include "ext/standard/url.h"
#include "ext/pcre/php_pcre.h"
#include "php_variables.h"
#include "php_buddel.h"
#include "Exception.h"

#define PHP_BUDDEL_SERVER_NAME    "Buddel HTTP Server"
#define PHP_BUDDEL_SERVER_VERSION "0.1.0"

#define PHP_BUDDEL_SERVER_RESPONSE_STATUS_NONE    0
#define PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENDING 1
#define PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENT    2

#define PHP_BUDDEL_SERVER_ROUTE_METHOD_GET        1
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_POST       2
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_HEAD       4
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_PUT        8
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_DELETE    16
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_OPTIONS   32
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_TRACE     64
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_CONNECT   128
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_PATCH     256
#define PHP_BUDDEL_SERVER_ROUTE_METHOD_ALL       511

extern zend_class_entry *ce_buddel_server;
extern zend_class_entry *ce_buddel_server_request;
extern zend_class_entry *ce_buddel_server_response;
extern zend_class_entry *ce_buddel_server_route;
extern zend_class_entry *ce_buddel_server_router;

struct php_buddel_server {
    zend_object std;
    zval refhandle;
    struct evhttp *http;
    char *addr;
    char *logformat;
    int logformat_len;
    php_stream *logfile;
    int port;
    int running;
    zval *router;
};

struct php_buddel_server_request {
    zend_object std;
    zval refhandle;
    struct evhttp_request *req;
    zval *cookies;
    zval *get;
    zval *post;
    zval *files;
    double time;
    int status;
    long response_len;
    long response_status;
    char *error;
    char *uri;
    char *query;
};

struct php_buddel_server_route {
    zend_object std;
    zval refhandle;
    char *route;
    char *regexp;
    zval *handler;
    int  methods;
};

struct php_buddel_server_router {
    zend_object std;
    zval refhandle;
    zval *routes;
    zval *methods;
};

struct php_buddel_server_logentry {
    double request_time;
    int    request_type;
    char * request_uri;
    char * uri;
    char * query;
    long   response_len;
    char * remote_host;
    long   remote_port;
    long   response_status;
    size_t mem_usage;
    char * error;
};

#define SETNOW(double_now) \
    double_now = 0.0;  struct timeval __tpnow = {0}; \
    if(gettimeofday(&__tpnow, NULL) == 0 ) \
        double_now = (double)(__tpnow.tv_sec + __tpnow. tv_usec / 1000000.00);

#define WRITELOG(server, msg, len) \
    if (server->logformat_len) { \
        if (server->logfile == NULL) { \
            write(STDOUT_FILENO, msg, len); \
        } else { \
            php_stream_write(server->logfile, msg, len); \
        } \
    }

#define LOGENTRY_CTOR(logentry, request) \
    logentry = (struct php_buddel_server_logentry *) ecalloc(1, sizeof(*logentry)); \
    logentry->request_time = request->time; \
    logentry->request_type = request->req->type; \
    logentry->request_uri = (char *)evhttp_request_uri(request->req); \
    logentry->uri = estrdup(request->uri ? request->uri : "-"); \
    logentry->query = estrdup(request->query ? request->query : "-"); \
    logentry->response_len = request->response_len; \
    logentry->remote_host = request->req->remote_host; \
    logentry->remote_port = request->req->remote_port; \
    logentry->response_status = request->response_status; \
    logentry->mem_usage = 0; \
    logentry->error = estrdup(request->error ? request->error : "-");

#define LOGENTRY_LOG(logentry, server, count) \
    double now; SETNOW(now); \
    zval *map; MAKE_STD_ZVAL(map); array_init(map); \
    if (php_buddel_strpos(server->logformat, "cs-uri", 0) != FAILURE) \
        add_assoc_string(map, "cs-uri", logentry->uri, 1); \
    if (php_buddel_strpos(server->logformat, "cs-query", 0) != FAILURE) \
        add_assoc_string(map, "cs-query", logentry->query, 1); \
    if (php_buddel_strpos(server->logformat, "c-ip", 0) != FAILURE) \
        add_assoc_string(map, "c-ip", logentry->remote_host, 1); \
    if (php_buddel_strpos(server->logformat, "c-port", 0) != FAILURE) \
        add_assoc_long(map, "c-port", logentry->remote_port); \
    if (php_buddel_strpos(server->logformat, "cs-method", 0) != FAILURE) \
        add_assoc_string(map, "cs-method", typeToMethod(logentry->request_type), 1); \
    if (php_buddel_strpos(server->logformat, "cs-uri", 0) != FAILURE) \
        add_assoc_string(map, "cs-uri", logentry->request_uri, 1); \
    if (php_buddel_strpos(server->logformat, "sc-status", 0) != FAILURE) \
        add_assoc_long(map, "sc-status", logentry->response_status); \
    if (php_buddel_strpos(server->logformat, "sc-bytes", 0) != FAILURE) { \
        if (logentry->response_len > 0) add_assoc_long(map, "sc-bytes", logentry->response_len); \
        else add_assoc_stringl(map, "sc-bytes", "-", 1, 1); \
    } \
    if (php_buddel_strpos(server->logformat, "x-reqnum", 0) != FAILURE) \
        add_assoc_long(map, "x-reqnum", count); \
    if (php_buddel_strpos(server->logformat, "x-memusage", 0) != FAILURE) \
        add_assoc_long(map, "x-memusage", zend_memory_usage(0 TSRMLS_CC)); \
    if (php_buddel_strpos(server->logformat, "time", 0) != FAILURE) { \
        char *str_time = php_format_date("H:i:s", sizeof("H:i:s"), (long)now, 1 TSRMLS_CC); \
        add_assoc_string(map, "time", str_time, 0); } \
    if (php_buddel_strpos(server->logformat, "date", 0) != FAILURE) { \
        char *str_time = php_format_date("Y-m-d", sizeof("Y-m-d"), (long)now, 1 TSRMLS_CC); \
        add_assoc_string(map, "date", str_time, 0); } \
    if (php_buddel_strpos(server->logformat, "time-taken", 0) != FAILURE) \
        add_assoc_long(map, "time-taken", (now - logentry->request_time) * 1000); \
    if (php_buddel_strpos(server->logformat, "bytes", 0) != FAILURE) { \
        if (logentry->response_len > 0) add_assoc_long(map, "bytes", logentry->response_len); \
        else add_assoc_stringl(map, "bytes", "-", 1, 1); \
    } \
    if (php_buddel_strpos(server->logformat, "x-error", 0) != FAILURE) \
        add_assoc_string(map, "x-error", logentry->error, 1); \
    zval *msg = php_buddel_strtr_array(server->logformat, server->logformat_len, Z_ARRVAL_P(map)); \
    WRITELOG(server, Z_STRVAL_P(msg), Z_STRLEN_P(msg)); \
    zval_ptr_dtor(&msg); \
    zval_ptr_dtor(&map);

#define LOGENTRY_DTOR(logentry) \
    efree(logentry->uri); \
    efree(logentry->query); \
    efree(logentry->error); \
    efree(logentry); 

PHP_MINIT_FUNCTION(buddel_server);
PHP_MSHUTDOWN_FUNCTION(buddel_server);
PHP_RINIT_FUNCTION(buddel_server);
PHP_RSHUTDOWN_FUNCTION(buddel_server);

#endif /* BUDDEL_HTTPSVC_SERVER_H */