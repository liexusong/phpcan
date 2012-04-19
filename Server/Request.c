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

#include "Server.h"

#ifndef HAVE_TAILQFOREACH
#include <sys/queue.h>
#endif

#include <event.h>
#include <evhttp.h>
#include <signal.h>
#include <sys/time.h>

zend_class_entry *ce_buddel_server_request;
static zend_object_handlers server_request_obj_handlers;

static void server_request_dtor(void *object TSRMLS_DC);

static zend_object_value server_request_ctor(zend_class_entry *ce TSRMLS_DC)
{
    struct php_buddel_server_request *r;
    zend_object_value retval;

    r = ecalloc(1, sizeof(*r));
    zend_object_std_init(&r->std, ce TSRMLS_CC);
    r->cookies = NULL;
    r->get = NULL;
    r->post = NULL;
    r->files = NULL;
    r->status = PHP_BUDDEL_SERVER_RESPONSE_STATUS_NONE;
    r->uri = NULL;
    r->query = NULL;
    r->response_status = 0;
    r->response_len = 0;
    r->error = NULL;
    retval.handle = zend_objects_store_put(r,
            (zend_objects_store_dtor_t)zend_objects_destroy_object,
            server_request_dtor,
            NULL TSRMLS_CC);
    retval.handlers = &server_request_obj_handlers;
    return retval;
}

static void server_request_dtor(void *object TSRMLS_DC)
{
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)object;

    if (r->req) {
        r->req = NULL;
    }

    if (r->cookies) {
        zval_ptr_dtor(&r->cookies);
    }

    if (r->get) {
        zval_ptr_dtor(&r->get);
    }

    if (r->post) {
        zval_ptr_dtor(&r->post);
    }

    if (r->files) {
        zval_ptr_dtor(&r->files);
    }

    if (r->uri) {
        efree(r->uri);
        r->uri = NULL;
    }

    if (r->query) {
        efree(r->query);
        r->query = NULL;
    }
    
    if (r->error) {
        efree(r->error);
        r->error = NULL;
    }

    zend_objects_store_del_ref(&r->refhandle TSRMLS_CC);
    zend_object_std_dtor(&r->std TSRMLS_CC);
    efree(r);
}

static char * typeToMethod(int type)
{
    switch (type) {
        case EVHTTP_REQ_GET:
            return "GET";
            break;
        case EVHTTP_REQ_POST:
            return "POST";
            break;
        case EVHTTP_REQ_HEAD:
            return "HEAD";
            break;
        case EVHTTP_REQ_PUT:
            return "PUT";
            break;
        case EVHTTP_REQ_DELETE:
            return "DELETE";
            break;
        case EVHTTP_REQ_OPTIONS:
            return "OPTIONS";
            break;
        case EVHTTP_REQ_TRACE:
            return "TRACE";
            break;
        case EVHTTP_REQ_CONNECT:
            return "CONNECT";
            break;
        case EVHTTP_REQ_PATCH:
            return "PATCH";
            break;
        default:
            return "Unknown";
            break;
    }
}

static zval *read_property(zval *object, zval *member, int type, const zend_literal *key TSRMLS_DC)
{
    struct php_buddel_server_request *r;
    zval tmp_member;
    zval *retval;
    zend_object_handlers *std_hnd;
    struct evkeyval *header;
    char * str;

    r = (struct php_buddel_server_request*)zend_object_store_get_object(object TSRMLS_CC);

    if (member->type != IS_STRING) {
        tmp_member = *member;
        zval_copy_ctor(&tmp_member);
        convert_to_string(&tmp_member);
        member = &tmp_member;
    }

    if (Z_STRLEN_P(member) == (sizeof("method") - 1)
            && !memcmp(Z_STRVAL_P(member), "method", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_STRING(retval, typeToMethod(r->req->type), 1);
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("uri") - 1)
            && !memcmp(Z_STRVAL_P(member), "uri", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_STRING(retval, r->uri != NULL ? r->uri : "", 1);
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("query") - 1)
            && !memcmp(Z_STRVAL_P(member), "query", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_STRING(retval, r->query != NULL ? r->query : "", 1);
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("protocol") - 1)
            && !memcmp(Z_STRVAL_P(member), "protocol", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        spprintf(&str, 0, "HTTP/%d.%d", r->req->major, r->req->minor);
        ZVAL_STRING(retval, str, 1);
        Z_SET_REFCOUNT_P(retval, 0);
        efree(str);

    } else if (Z_STRLEN_P(member) == (sizeof("remote_addr") - 1)
            && !memcmp(Z_STRVAL_P(member), "remote_addr", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        if (r->req->remote_host) {
            ZVAL_STRING(retval, r->req->remote_host, 1);
        } else {
            ZVAL_STRING(retval, "", 1);
        }
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("remote_port") - 1)
            && !memcmp(Z_STRVAL_P(member), "remote_port", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_LONG(retval, (int)r->req->remote_port);
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("headers") - 1)
            && !memcmp(Z_STRVAL_P(member), "headers", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        array_init(retval);
        /*
        TAILQ_FOREACH( header, r->req->input_headers, next)
        {
            add_assoc_string(retval, header->key, header->value, 1);
        }
        */
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("cookies") - 1)
            && !memcmp(Z_STRVAL_P(member), "cookies", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        array_init(retval);
        if (r->cookies) {
            zval *tmp;
            zend_hash_copy(
                Z_ARRVAL_P(retval), Z_ARRVAL_P(r->cookies),
                (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
            );
        }
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("get") - 1)
            && !memcmp(Z_STRVAL_P(member), "get", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        array_init(retval);
        if (r->get) {
            zval *tmp;
            zend_hash_copy(
                Z_ARRVAL_P(retval), Z_ARRVAL_P(r->get),
                (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
            );
        }
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("post") - 1)
            && !memcmp(Z_STRVAL_P(member), "post", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        array_init(retval);
        if (r->post) {
            zval *tmp;
            zend_hash_copy(
                Z_ARRVAL_P(retval), Z_ARRVAL_P(r->post),
                (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
            );
        }
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("files") - 1)
            && !memcmp(Z_STRVAL_P(member), "files", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        array_init(retval);
        if (r->files) {
            zval *tmp;
            zend_hash_copy(
                Z_ARRVAL_P(retval), Z_ARRVAL_P(r->files),
                (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
            );
        }
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("status") - 1)
            && !memcmp(Z_STRVAL_P(member), "status", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_LONG(retval, (int)r->status);
        Z_SET_REFCOUNT_P(retval, 0);

    } else if (Z_STRLEN_P(member) == (sizeof("time") - 1)
            && !memcmp(Z_STRVAL_P(member), "time", Z_STRLEN_P(member))) {

        MAKE_STD_ZVAL(retval);
        ZVAL_DOUBLE(retval, r->time);
        Z_SET_REFCOUNT_P(retval, 0);

    } else {
        std_hnd = zend_get_std_object_handlers();
        retval = std_hnd->read_property(object, member, type, key TSRMLS_CC);
    }

    if (member == &tmp_member) {
        zval_dtor(member);
    }

    return retval;
}

static HashTable *get_properties(zval *object TSRMLS_DC) /* {{{ */
{

    HashTable *props;
    zval *zv;
    char *str;
    struct evkeyval *header;
    
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_objects_get_address(object TSRMLS_CC);
    
    
    props = zend_std_get_properties(object TSRMLS_CC);
    
    MAKE_STD_ZVAL(zv);
    ZVAL_STRING(zv, typeToMethod(r->req->type), 1);
    zend_hash_update(props, "method", sizeof("method"), &zv, sizeof(zval), NULL);
    
    MAKE_STD_ZVAL(zv);
    ZVAL_STRING(zv, r->uri != NULL ? r->uri : "", 1);
    zend_hash_update(props, "uri", sizeof("uri"), &zv, sizeof(zval), NULL);
    
    MAKE_STD_ZVAL(zv);
    ZVAL_STRING(zv, r->query != NULL ? r->query : "", 1);
    zend_hash_update(props, "query", sizeof("query"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    spprintf(&str, 0, "HTTP/%d.%d", r->req->major, r->req->minor);
    ZVAL_STRING(zv, str, 1);
    zend_hash_update(props, "protocol", sizeof("protocol"), &zv, sizeof(zval), NULL);
    efree(str);

    MAKE_STD_ZVAL(zv);
    ZVAL_STRING(zv, r->req->remote_host ? r->req->remote_host : "", 1);
    zend_hash_update(props, "remote_addr", sizeof("remote_addr"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    ZVAL_LONG(zv, (int)r->req->remote_port);
    zend_hash_update(props, "remote_port", sizeof("remote_port"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    array_init(zv);
    /*
    TAILQ_FOREACH (header, r->req->input_headers, next)
    {
        add_assoc_string(zv, header->key, header->value, 1);
    }
    */
    zend_hash_update(props, "headers", sizeof("headers"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    array_init(zv);
    if (r->cookies) {
        zval *tmp;
        zend_hash_copy(
            Z_ARRVAL_P(zv), Z_ARRVAL_P(r->cookies),
            (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
        );
    }
    zend_hash_update(props, "cookies", sizeof("cookies"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    array_init(zv);
    if (r->get) {
        zval *tmp;
        zend_hash_copy(
            Z_ARRVAL_P(zv), Z_ARRVAL_P(r->get),
            (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
        );
    }
    zend_hash_update(props, "get", sizeof("get"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    array_init(zv);
    if (r->post) {
        zval *tmp;
        zend_hash_copy(
            Z_ARRVAL_P(zv), Z_ARRVAL_P(r->post),
            (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
        );
    }
    zend_hash_update(props, "post", sizeof("post"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    array_init(zv);
    if (r->files) {
        zval *tmp;
        zend_hash_copy(
            Z_ARRVAL_P(zv), Z_ARRVAL_P(r->files),
            (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *)
        );
    }
    zend_hash_update(props, "files", sizeof("files"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    ZVAL_LONG(zv, (int)r->status);
    zend_hash_update(props, "status", sizeof("status"), &zv, sizeof(zval), NULL);

    MAKE_STD_ZVAL(zv);
    ZVAL_DOUBLE(zv, r->time);
    zend_hash_update(props, "time", sizeof("time"), &zv, sizeof(zval), NULL);
    
    return props;
}

/**
 * Constructor
 */
static PHP_METHOD(BuddelServerRequest, __construct)
{
    /* final protected */
}

/**
 * Find request header
 */
static PHP_METHOD(BuddelServerRequest, findRequestHeader)
{
    char *header;
    int header_len;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "s", &header, &header_len)) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $header)",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }
    
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    const char *value =evhttp_find_header(r->req->input_headers, (const char*)header);
    if (value == NULL) {
        RETURN_FALSE;
    }
    RETURN_STRING(value, 1);
}

/**
 * Get raw request data
 */
static PHP_METHOD(BuddelServerRequest, getRequestBody)
{
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    int buffer_len = EVBUFFER_LENGTH(r->req->input_buffer);
    if (buffer_len > 0) {
        RETURN_STRINGL(EVBUFFER_DATA(r->req->input_buffer), buffer_len, 1);
    }
    RETURN_FALSE;
}

/**
 * Add response header
 *
 *
 */
static PHP_METHOD(BuddelServerRequest, addResponseHeader)
{
    char *header, *value;
    int header_len, value_len;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "ss", &header, &header_len, &value, &value_len)) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $header, string $value)",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }

    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    if (evhttp_add_header(r->req->output_headers, header, value) != 0) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}

/**
 * Remove response header
 *
 *
 */
static PHP_METHOD(BuddelServerRequest, removeResponseHeader)
{
    char *header, *value;
    int header_len, value_len;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "s", &header, &header_len)) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $header)",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }

    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    if (evhttp_remove_header(r->req->output_headers, header) != 0) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}

/**
 * Set response status
 *
 *
 */
static PHP_METHOD(BuddelServerRequest, setResponseStatus)
{
    long status;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "l", &status)) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(int $status)",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }
    
    if (status < 100 || status > 599) {
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "Expection valid HTTP status (100-599)"
        );
        return;
    }

    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    r->response_status = status;
}

/**
 * Redirect client to new location
 */
static PHP_METHOD(BuddelServerRequest, redirect)
{
    char *location;
    int *location_len = 0;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "s", &location, &location_len) || location_len == 0) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $location)",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }
    
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    if (evhttp_add_header(r->req->output_headers, "Location", location) != 0) {
        RETURN_FALSE;
    }
    r->response_status = 302;
    RETURN_TRUE;
}

/**
 * Set cookie
 */
static PHP_METHOD(BuddelServerRequest, setCookie)
{
    char *name, *value, *path, *domain;
    int name_len = 0, value_len = 0, path_len = 0, domain_len = 0;
    long expires = 0;
    zend_bool secure = 0, httponly= 0, url_encode = 0; 

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "ss|slssbb", &name, &name_len, &value, &value_len, &expires, &path, &path_len, 
            &domain, &domain_len, &secure, &httponly) || name_len == 0
    ) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $name [, string $value [, int $expire = 0 [, string $path "
            "[, string $domain [, bool $secure = false [, bool $httponly = false ]]]]]])",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }
    
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);

    char *cookie, *encoded_value = NULL;
    int len = name_len;
    char *dt;
    int result;

    if (name && strpbrk(name, "=,; \t\r\n\013\014") != NULL) {   /* man isspace for \013 and \014 */
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "Cookie names cannot contain any of the following '=,; \\t\\r\\n\\013\\014'"
        );
        return;
    }

    if (!url_encode && value && strpbrk(value, ",; \t\r\n\013\014") != NULL) { /* man isspace for \013 and \014 */
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "Cookie values cannot contain any of the following '=,; \\t\\r\\n\\013\\014'"
        );
        return;
    }

    if (value && url_encode) {
        int encoded_value_len;
        encoded_value = php_url_encode(value, value_len, &encoded_value_len);
        len += encoded_value_len;
    } else if ( value ) {
        encoded_value = estrdup(value);
        len += value_len;
    }
    if (path) {
        len += path_len;
    }
    if (domain) {
        len += domain_len;
    }

    cookie = emalloc(len + 100);

    if (value && value_len == 0) {
        /* 
         * MSIE doesn't delete a cookie when you set it to a null value
         * so in order to force cookies to be deleted, even on MSIE, we
         * pick an expiry date in the past
         */
        dt = php_format_date("D, d-M-Y H:i:s T", sizeof("D, d-M-Y H:i:s T")-1, 1, 0 TSRMLS_CC);
        snprintf(cookie, len + 100, "%s=deleted; expires=%s", name, dt);
        efree(dt);
    } else {
        snprintf(cookie, len + 100, "%s=%s", name, value ? encoded_value : "");
        if (expires > 0) {
            const char *p;
            strlcat(cookie, "; expires=", len + 100);
            dt = php_format_date("D, d-M-Y H:i:s T", sizeof("D, d-M-Y H:i:s T")-1, expires, 0 TSRMLS_CC);
            /* check to make sure that the year does not exceed 4 digits in length */
            p = zend_memrchr(dt, '-', strlen(dt));
            if (!p || *(p + 5) != ' ') {
                    efree(dt);
                    efree(cookie);
                    efree(encoded_value);
                    php_buddel_throw_exception(
                        ce_buddel_InvalidParametersException TSRMLS_CC,
                        "Expiry date cannot have a year greater then 9999"
                    );
                    return;
            }
            strlcat(cookie, dt, len + 100);
            efree(dt);
        }
    }
    
    if (encoded_value) {
        efree(encoded_value);
    }

    if (path && path_len > 0) {
        strlcat(cookie, "; path=", len + 100);
        strlcat(cookie, path, len + 100);
    }
    if (domain && domain_len > 0) {
        strlcat(cookie, "; domain=", len + 100);
        strlcat(cookie, domain, len + 100);
    }
    if (secure) {
        strlcat(cookie, "; secure", len + 100);
    }
    if (httponly) {
        strlcat(cookie, "; httponly", len + 100);
    }
    
    if (evhttp_add_header(r->req->output_headers, "Set-Cookie", cookie) != 0) {
        RETVAL_FALSE;
    } else {
        RETVAL_TRUE;
    }
    efree(cookie);
}

/**
 * Get realpath of the given filename
 */
static char *get_realpath(char *filename TSRMLS_DC)
{
    char resolved_path_buff[MAXPATHLEN];
    if (VCWD_REALPATH(filename, resolved_path_buff)) {
        if (php_check_open_basedir(resolved_path_buff TSRMLS_CC)) {
            return NULL;
        }
#ifdef ZTS
        if (VCWD_ACCESS(resolved_path_buff, F_OK)) {
            return NULL;
        }
#endif
        return estrdup(resolved_path_buff);
    }
    return NULL;
}

/**
 * Send file
 */
static PHP_METHOD(BuddelServerRequest, sendFile)
{
    char *filename, *root, *mimetype;
    int filename_len, root_len = 0, *mimetype_len = 0;
    zval *download = NULL;

    if (FAILURE == zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS() TSRMLS_CC,
            "p|psz", &filename, &filename_len, &root, &root_len, 
                     &mimetype, &mimetype_len, &download)) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(string $filename[, string $root[, string $mimetype[, string $download]]])",
            class_name, space, get_active_function_name(TSRMLS_C)
        );
        return;
    }
    
    char *path = NULL;
    if (root_len > 0) {
        path = get_realpath(root TSRMLS_CC);
        if (path == NULL) {
            const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
            php_buddel_throw_exception(
                ce_buddel_InvalidParametersException TSRMLS_CC,
                "%s%s%s(): Cannot determine real path of '%s'",
                class_name, space, get_active_function_name(TSRMLS_C),
                root
            );
            return;
        }
    }
    
    char *filepath = NULL;
    spprintf(&filepath, 0, "%s%s%s", path ? path : "", (path && filename[0] != '/' ? "/" : ""), filename);
    if (path != NULL) {
        efree(path);
    }
    path = get_realpath(filepath TSRMLS_CC);
    if (path == NULL) {
        const char *space, *class_name = get_active_class_name(&space TSRMLS_CC);
        php_buddel_throw_exception(
            ce_buddel_InvalidParametersException TSRMLS_CC,
            "%s%s%s(): Cannot determine real path of file '%s'",
            class_name, space, get_active_function_name(TSRMLS_C),
            filepath
        );
        efree(filepath);
        return;
    }
    
    struct php_buddel_server_request *r = (struct php_buddel_server_request*)
        zend_object_store_get_object(getThis() TSRMLS_CC);
    
    if (mimetype_len == 0) {
        
        // try to determine mimetype with finfo
        zend_class_entry **cep;
        if (zend_lookup_class_ex("\\finfo", sizeof("\\finfo") - 1, NULL, 0, &cep TSRMLS_CC) == SUCCESS) {

            zval *retval_ptr, *object, **params[1], *arg, *zfilepath, *retval;
            zend_fcall_info fci;
            zend_fcall_info_cache fcc;
            zend_class_entry *ce = *cep;
            
            // create finfo object
            ALLOC_INIT_ZVAL(object);
            object_init_ex(object, ce);

            MAKE_STD_ZVAL(arg);
            ZVAL_LONG(arg, 0x000010|0x000400); // MAGIC_MIME_TYPE|MAGIC_MIME_ENCODING
            params[0] = &arg;

            fci.size = sizeof(fci);
            fci.function_table = EG(function_table);
            fci.function_name = NULL;
            fci.symbol_table = NULL;
            fci.object_ptr = object;
            fci.retval_ptr_ptr = &retval_ptr;
            fci.param_count = 1;
            fci.params = params;
            fci.no_separation = 1;

            fcc.initialized = 1;
            fcc.function_handler = ce->constructor;
            fcc.calling_scope = EG(scope);
            fcc.called_scope = Z_OBJCE_P(object);
            fcc.object_ptr = object;

            int result = zend_call_function(&fci, &fcc TSRMLS_CC);
            zval_ptr_dtor(&arg);
            if (retval_ptr) {
                zval_ptr_dtor(&retval_ptr);
            }
            
            if (result == FAILURE) {
                php_buddel_throw_exception(
                    ce_buddel_RuntimeException TSRMLS_CC,
                    "Failed to call '%s' constructor",
                    ce->name
                );
                
                efree(filepath);
                if (path != NULL) {
                    efree(path);
                }
                return;
            }
            
            // call finfo->file(filename)
            MAKE_STD_ZVAL(zfilepath);
            ZVAL_STRING(zfilepath, path, 1);
            zend_call_method_with_1_params(&object, Z_OBJCE_P(object), NULL, "file", &retval, zfilepath);
            zval_ptr_dtor(&zfilepath);
            if (EG(exception)) {
                efree(filepath);
                if (path != NULL) {
                    efree(path);
                }
                return;
            }

            evhttp_add_header(r->req->output_headers, "Content-Type", Z_STRVAL_P(retval));
            zval_ptr_dtor(&retval);
            zval_ptr_dtor(&object);
            
        } else {
            evhttp_add_header(r->req->output_headers, "Content-Type", "text/plain");
        }
        
    } else {
        evhttp_add_header(r->req->output_headers, "Content-Type", mimetype);
    }
    
    // handle downloads
    if (download) { 
        char *basename = NULL;
        size_t basename_len;
        if (Z_TYPE_P(download) == IS_BOOL && Z_BVAL_P(download) == 1) { 
            php_basename(filepath, strlen(filepath), NULL, 0, &basename, &basename_len TSRMLS_CC);
        } else if (Z_TYPE_P(download) == IS_STRING) {
            basename = estrndup(Z_STRVAL_P(download), Z_STRLEN_P(download));
        }
        if (basename) {
            spprintf(&basename, 0, "attachment; filename=\"%s\"", basename);
            evhttp_add_header(r->req->output_headers, "Content-Disposition", basename);
            efree(basename);
        }
    }
    
    efree(filepath);
    
    php_stream *stream = php_stream_open_wrapper(path, "rb", ENFORCE_SAFE_MODE | REPORT_ERRORS, NULL);
    if (!stream) {
        php_buddel_throw_exception(
            ce_buddel_RuntimeException TSRMLS_CC,
            "Cannot read content of the file '%s'", path
        );
        if (path != NULL) {
            efree(path);
        }
        return;
    }
    
    // get file stat
    php_stream_statbuf st;
    if (php_stream_stat(stream, &st) < 0) {
        php_buddel_throw_exception(
            ce_buddel_RuntimeException TSRMLS_CC,
            "Cannot stat of the file '%s'", path
        );
        if (path != NULL) {
            efree(path);
        }
        return;
    }
    
    // generate ETag
    char *etag = NULL;
    spprintf(&etag, 0, "%X-%X-%X", (int)st.sb.st_ino, (int)st.sb.st_mtime, (int)st.sb.st_size);
    
    const char *client_etag = evhttp_find_header(r->req->input_headers, "If-None-Match");
    if (client_etag != NULL && strcmp(client_etag, etag) == 0) {
        
        // ETags are the same 
        r->response_status = 304;
        evhttp_send_reply(r->req, r->response_status, NULL, NULL);
        
    } else {
        
        const char *client_lm = evhttp_find_header(r->req->input_headers, "If-Modified-Since");
        int client_ts = 0;
        if (client_lm != NULL) {
            zval retval, *strtotime, *time, *args[1];
            MAKE_STD_ZVAL(strtotime); ZVAL_STRING(strtotime, "strtotime", 1);
            MAKE_STD_ZVAL(time); ZVAL_STRING(time, client_lm, 1);
            args[0] = time; Z_ADDREF_P(args[0]);
            if (call_user_function(EG(function_table), NULL, strtotime, &retval, 1, args TSRMLS_CC) == SUCCESS) {
                if (Z_TYPE(retval) == IS_LONG) {
                    client_ts = Z_LVAL(retval);
                }
                zval_dtor(&retval);
            }
            Z_DELREF_P(args[0]);
            zval_ptr_dtor(&time);
            zval_ptr_dtor(&strtotime);
        }
        
        if (client_ts >= st.sb.st_mtime) {
            
            // not modified
            r->response_status = 304;
            evhttp_send_reply(r->req, r->response_status, NULL, NULL);

        } else {
            
            // add ETag header
            evhttp_add_header(r->req->output_headers, "ETag", etag);

            // generate and add Last-Modified header
            char *lm = NULL;
            zval retval, *gmstrftime, *format, *timestamp, *args[2];
            MAKE_STD_ZVAL(gmstrftime); ZVAL_STRING(gmstrftime, "gmstrftime", 1);
            MAKE_STD_ZVAL(format); ZVAL_STRING(format, "%a, %d %b %Y %H:%M:%S GMT", 1);
            MAKE_STD_ZVAL(timestamp); ZVAL_LONG(timestamp, st.sb.st_mtime);
            args[0] = format; args[1] = timestamp;
            Z_ADDREF_P(args[0]); Z_ADDREF_P(args[1]);
            if (call_user_function(EG(function_table), NULL, gmstrftime, &retval, 2, args TSRMLS_CC) == SUCCESS) {
                if (Z_TYPE(retval) == IS_STRING) {
                    lm = estrndup(Z_STRVAL(retval), Z_STRLEN(retval));
                }
                zval_dtor(&retval);
            }
            Z_DELREF_P(args[0]); Z_DELREF_P(args[1]);
            zval_ptr_dtor(&format);
            zval_ptr_dtor(&timestamp);
            zval_ptr_dtor(&gmstrftime);
        
            if (lm != NULL) {
                evhttp_add_header(r->req->output_headers, "Last-Modified", lm);
                efree(lm);
            }

            // TODO: Accept-Ranges implementation
            //evhttp_add_header(r->req->output_headers, "Accept-Ranges", "bytes");
            //const char *range = evhttp_find_header(r->req->input_headers, "Range");
            
            if (st.sb.st_size < 524288) {
                
                // send as whole file
                char *content;
                int content_len = php_stream_copy_to_mem(stream, &content, st.sb.st_size, 0);
                r->response_status = 200;
                r->response_len = content_len;
                struct evbuffer *buffer = evbuffer_new();
                evbuffer_add(buffer, content, content_len);
                evhttp_send_reply(r->req, 200, NULL, buffer);
                evbuffer_free(buffer);
                efree(content);
                
            } else {
                
                // send chunked
                long pos = 0;
                char *chunk;
                while(-1 != php_stream_seek(stream, pos, SEEK_SET )) {
                    if (pos == 0) {
                        r->response_status = 200;
                        evhttp_send_reply_start(r->req, r->response_status, NULL);
                        r->status = PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENDING;
                        r->response_len = 0;
                    }
                    int chunk_len = php_stream_copy_to_mem(stream, &chunk, 1024, 0);
                    if (chunk_len == 0) {
                        efree(chunk);
                        break;
                    }
                    struct evbuffer *buffer = evbuffer_new();
                    evbuffer_add(buffer, chunk, chunk_len);
                    evhttp_send_reply_chunk(r->req, buffer);
                    evbuffer_free(buffer);
                    efree(chunk);
                    r->response_len += chunk_len;
                    pos += chunk_len;
                }
                evhttp_send_reply_end(r->req);
            }
        }
    }
    r->status = PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENT;
    
    efree(etag);
    php_stream_close(stream);
    if (path != NULL) {
        efree(path);
    }
}

static zend_function_entry server_request_methods[] = {
    PHP_ME(BuddelServerRequest, __construct,          NULL, ZEND_ACC_FINAL | ZEND_ACC_PROTECTED)
    PHP_ME(BuddelServerRequest, findRequestHeader,    NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, getRequestBody,       NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, addResponseHeader,    NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, removeResponseHeader, NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, setResponseStatus,    NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, redirect,             NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, setCookie,            NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    PHP_ME(BuddelServerRequest, sendFile,             NULL, ZEND_ACC_FINAL | ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};

static void server_request_init(TSRMLS_D)
{
    memcpy(&server_request_obj_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    server_request_obj_handlers.clone_obj = NULL;
    server_request_obj_handlers.read_property = read_property;
    server_request_obj_handlers.get_properties = get_properties;
    
    // class \Buddel\Server\Request
    PHP_BUDDEL_REGISTER_CLASS(
        &ce_buddel_server_request,
        ZEND_NS_NAME(PHP_BUDDEL_SERVER_NS, "Request"),
        server_request_ctor,
        server_request_methods
    );

    PHP_BUDDEL_REGISTER_CLASS_CONST_LONG(ce_buddel_server_request, "STATUS_NONE",
        PHP_BUDDEL_SERVER_RESPONSE_STATUS_NONE);
    PHP_BUDDEL_REGISTER_CLASS_CONST_LONG(ce_buddel_server_request, "STATUS_SENDING",
        PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENDING);
    PHP_BUDDEL_REGISTER_CLASS_CONST_LONG(ce_buddel_server_request, "STATUS_SENT",
        PHP_BUDDEL_SERVER_RESPONSE_STATUS_SENT);
}

PHP_MINIT_FUNCTION(buddel_server_request)
{
    server_request_init(TSRMLS_C);
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(buddel_server_request)
{
    return SUCCESS;
}

PHP_RINIT_FUNCTION(buddel_server_request)
{
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(buddel_server_request)
{
    return SUCCESS;
}