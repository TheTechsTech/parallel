/*
  +----------------------------------------------------------------------+
  | parallel                                                              |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2019                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe                                                      |
  +----------------------------------------------------------------------+
 */
#ifndef HAVE_PARALLEL_FUTURE
#define HAVE_PARALLEL_FUTURE

#include "parallel.h"

zend_class_entry *php_parallel_future_ce;
zend_object_handlers php_parallel_future_handlers;

zend_bool php_parallel_future_lock(php_parallel_future_t *future) {
    return php_parallel_monitor_lock(future->monitor);
}

zend_bool php_parallel_future_readable(php_parallel_future_t *future) {
    return php_parallel_monitor_check(future->monitor, PHP_PARALLEL_READY);
}

void php_parallel_future_value(php_parallel_future_t *future, zval *return_value) {
    if (!Z_ISUNDEF(future->saved)) {
		ZVAL_COPY(return_value, &future->saved);
		return;
	}
	
    if (Z_TYPE(future->value) != IS_NULL) {
		php_parallel_copy_zval(return_value, &future->value, 0);

		if (Z_REFCOUNTED(future->value)) {
			php_parallel_zval_dtor(&future->value);
		}

		ZVAL_COPY(&future->saved, return_value);
	} else {
		ZVAL_NULL(&future->saved);
	}

	php_parallel_monitor_set(future->monitor, PHP_PARALLEL_DONE, 0);
}

zend_bool php_parallel_future_unlock(php_parallel_future_t *future) {
    return php_parallel_monitor_unlock(future->monitor);
}

PHP_METHOD(Future, value) 
{
	php_parallel_future_t *future = php_parallel_future_from(getThis());
	int32_t state;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_QUIET, 0, 0)
	ZEND_PARSE_PARAMETERS_END_EX(
	    php_parallel_exception(
	        "expected no arguments");
	    return;
	);

    if (php_parallel_monitor_check(future->monitor, PHP_PARALLEL_DONE)) {
        goto _php_parallel_future_value;
    } else {
        state = php_parallel_monitor_wait(future->monitor, 
				    PHP_PARALLEL_READY|
				    PHP_PARALLEL_FAILURE|
				    PHP_PARALLEL_KILLED|
				    PHP_PARALLEL_ERROR);
    }

	if ((state == FAILURE) || (state & PHP_PARALLEL_FAILURE)) {
		php_parallel_exception_ex(
		    php_parallel_future_error_ce,
			"cannot retrieve value");
		php_parallel_monitor_set(future->monitor, 
		    PHP_PARALLEL_READY|PHP_PARALLEL_FAILURE, 0);
		return;
	}

	if ((state & PHP_PARALLEL_KILLED)) {
		php_parallel_exception_ex(
		    php_parallel_future_error_killed_ce,
			"cannot retrieve value");
		php_parallel_monitor_set(future->monitor, 
		    PHP_PARALLEL_READY|PHP_PARALLEL_KILLED, 0);
		return;
	}

	if ((state & PHP_PARALLEL_ERROR)) {
		php_parallel_exception_ex(
		    php_parallel_future_error_uncaught_ce,
			"cannot retrieve value");
		php_parallel_monitor_set(future->monitor, 
		    PHP_PARALLEL_READY|PHP_PARALLEL_ERROR, 0);
		return;
	}
	
	php_parallel_monitor_set(
	    future->monitor, PHP_PARALLEL_READY, 0);

_php_parallel_future_value:
	php_parallel_future_value(future, return_value);
}

PHP_METHOD(Future, done)
{
	php_parallel_future_t *future = 
		php_parallel_future_from(getThis());

	RETURN_BOOL(php_parallel_monitor_check(future->monitor, PHP_PARALLEL_READY));
}

zend_function_entry php_parallel_future_methods[] = {
	PHP_ME(Future, value, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Future, done, NULL, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

zend_object* php_parallel_future_create(zend_class_entry *type) {
	php_parallel_future_t *future = ecalloc(1, 
			sizeof(php_parallel_future_t) + zend_object_properties_size(type));

	zend_object_std_init(&future->std, type);

	future->std.handlers = &php_parallel_future_handlers;

	future->monitor = php_parallel_monitor_create();

	return &future->std;
}

void php_parallel_future_destroy(zend_object *o) {
	php_parallel_future_t *future = 
		php_parallel_future_fetch(o);

	if (!php_parallel_monitor_check(future->monitor, PHP_PARALLEL_DONE)) {
		php_parallel_monitor_wait(future->monitor, PHP_PARALLEL_READY);
		
		if (Z_REFCOUNTED(future->value)) {
			php_parallel_zval_dtor(&future->value);
		}
	}

	if (Z_REFCOUNTED(future->saved))
		zval_ptr_dtor(&future->saved);

	php_parallel_monitor_destroy(future->monitor);

	zend_object_std_dtor(o);
}

void php_parallel_future_startup() {
	zend_class_entry ce;

	memcpy(&php_parallel_future_handlers, php_parallel_standard_handlers(), sizeof(zend_object_handlers));

	php_parallel_future_handlers.offset = XtOffsetOf(php_parallel_future_t, std);
	php_parallel_future_handlers.free_obj = php_parallel_future_destroy;

	INIT_NS_CLASS_ENTRY(ce, "parallel", "Future", php_parallel_future_methods);

	php_parallel_future_ce = zend_register_internal_class(&ce);
	php_parallel_future_ce->create_object = php_parallel_future_create;
	php_parallel_future_ce->ce_flags |= ZEND_ACC_FINAL;
}
#endif
