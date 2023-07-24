#include "batch.h"
#include "php_grpc.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <zend_API.h>
#include <zend_exceptions.h>
#include <stdbool.h>
#include <grpc/support/alloc.h>
#include <grpc/grpc.h>

#include "byte_buffer.h"

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
zval *grpc_parse_metadata_array(grpc_metadata_array
                                *metadata_array TSRMLS_DC) {
  int count = metadata_array->count;
  grpc_metadata *elements = metadata_array->metadata;
  zval *array;
  PHP_GRPC_MAKE_STD_ZVAL(array);
  array_init(array);
  int i;
  HashTable *array_hash;
  zval *inner_array;
  char *str_key;
  char *str_val;
  size_t key_len;
  zval *data = NULL;

  array_hash = Z_ARRVAL_P(array);
  grpc_metadata *elem;
  for (i = 0; i < count; i++) {
    elem = &elements[i];
    key_len = GRPC_SLICE_LENGTH(elem->key);
    str_key = ecalloc(key_len + 1, sizeof(char));
    memcpy(str_key, GRPC_SLICE_START_PTR(elem->key), key_len);
    str_val = ecalloc(GRPC_SLICE_LENGTH(elem->value) + 1, sizeof(char));
    memcpy(str_val, GRPC_SLICE_START_PTR(elem->value),
           GRPC_SLICE_LENGTH(elem->value));
    if (php_grpc_zend_hash_find(array_hash, str_key, key_len, (void **)&data)
        == SUCCESS) {
      if (Z_TYPE_P(data) != IS_ARRAY) {
        zend_throw_exception(zend_exception_get_default(TSRMLS_C),
                             "Metadata hash somehow contains wrong types.",
                             1 TSRMLS_CC);
        efree(str_key);
        efree(str_val);
        PHP_GRPC_FREE_STD_ZVAL(array);
        return NULL;
      }
      php_grpc_add_next_index_stringl(data, str_val,
                                      GRPC_SLICE_LENGTH(elem->value),
                                      false);
    } else {
      PHP_GRPC_MAKE_STD_ZVAL(inner_array);
      array_init(inner_array);
      php_grpc_add_next_index_stringl(inner_array, str_val,
                                      GRPC_SLICE_LENGTH(elem->value), false);
      add_assoc_zval(array, str_key, inner_array);
      PHP_GRPC_FREE_STD_ZVAL(inner_array);
    }
    efree(str_key);
    efree(str_val);
  }
  return array;
}

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool create_metadata_array(zval *array, grpc_metadata_array *metadata) {
  HashTable *array_hash;
  HashTable *inner_array_hash;
  zval *value;
  zval *inner_array;
  grpc_metadata_array_init(metadata);
  metadata->count = 0;
  metadata->metadata = NULL;
  if (Z_TYPE_P(array) != IS_ARRAY) {
    return false;
  }
  array_hash = Z_ARRVAL_P(array);

  char *key;
  int key_type;
  PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(array_hash, key, key_type,
                                          inner_array)
    if (key_type != HASH_KEY_IS_STRING || key == NULL) {
      return false;
    }
    if (Z_TYPE_P(inner_array) != IS_ARRAY) {
      return false;
    }
    inner_array_hash = Z_ARRVAL_P(inner_array);
    metadata->capacity += zend_hash_num_elements(inner_array_hash);
  PHP_GRPC_HASH_FOREACH_END()

  metadata->metadata = gpr_malloc(metadata->capacity * sizeof(grpc_metadata));

  char *key1 = NULL;
  int key_type1;
  PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(array_hash, key1, key_type1,
                                          inner_array)
    if (key_type1 != HASH_KEY_IS_STRING) {
      return false;
    }
    if (!grpc_header_key_is_legal(grpc_slice_from_static_string(key1))) {
      return false;
    }
    inner_array_hash = Z_ARRVAL_P(inner_array);
    PHP_GRPC_HASH_FOREACH_VAL_START(inner_array_hash, value)
      if (Z_TYPE_P(value) != IS_STRING) {
        return false;
      }
      metadata->metadata[metadata->count].key =
        grpc_slice_from_copied_string(key1);
      metadata->metadata[metadata->count].value =
        grpc_slice_from_copied_buffer(Z_STRVAL_P(value), Z_STRLEN_P(value));
      metadata->count += 1;
    PHP_GRPC_HASH_FOREACH_END()
  PHP_GRPC_HASH_FOREACH_END()
  return true;
}

struct batch* batch_new(grpc_call *wrapped) {
  struct batch* batch = ecalloc(1, sizeof(*batch));
  batch->wrapped = wrapped;
  grpc_metadata_array_init(&batch->metadata);
  grpc_metadata_array_init(&batch->trailing_metadata);
  grpc_metadata_array_init(&batch->recv_metadata);
  grpc_metadata_array_init(&batch->recv_trailing_metadata);
  batch->recv_status_details = grpc_empty_slice();
  batch->send_status_details = grpc_empty_slice();
  return batch;
}

void batch_destroy(struct batch* batch) {
  if (batch == NULL) {
    return;
  }
  grpc_metadata_array_destroy(&batch->metadata);
  grpc_metadata_array_destroy(&batch->trailing_metadata);
  grpc_metadata_array_destroy(&batch->recv_metadata);
  grpc_metadata_array_destroy(&batch->recv_trailing_metadata);
  grpc_slice_unref(batch->recv_status_details);
  grpc_slice_unref(batch->send_status_details);
  zval_dtor(&batch->fci.function_name);
  for (int i = 0; i < batch->op_num; i++) {
    if (batch->ops[i].op == GRPC_OP_SEND_MESSAGE) {
      grpc_byte_buffer_destroy(batch->ops[i].data.send_message.send_message);
    }
    if (batch->ops[i].op == GRPC_OP_RECV_MESSAGE) {
      grpc_byte_buffer_destroy(batch->message);
    }
  }
  efree(batch);
}

void batch_consume(struct batch* batch, zval *result) {
  zval *recv_status;
  zval *recv_md;
  zend_string* zmessage;

  object_init(result);

  for (int i = 0; i < batch->op_num; i++) {
    switch(batch->ops[i].op) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      add_property_bool(result, "send_metadata", true);
      break;
    case GRPC_OP_SEND_MESSAGE:
      add_property_bool(result, "send_message", true);
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      add_property_bool(result, "send_close", true);
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      add_property_bool(result, "send_status", true);
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
      recv_md = grpc_parse_metadata_array(&batch->recv_metadata);
      add_property_zval(result, "metadata", recv_md);
      zval_ptr_dtor(recv_md);
      PHP_GRPC_FREE_STD_ZVAL(recv_md);
      PHP_GRPC_DELREF(array);
      break;
    case GRPC_OP_RECV_MESSAGE:
      zmessage = byte_buffer_to_zend_string(batch->message);

      if (zmessage == NULL) {
        add_property_null(result, "message");
      } else {
        zval zmessage_val;
        ZVAL_NEW_STR(&zmessage_val, zmessage);
        add_property_zval(result, "message", &zmessage_val);
        zval_ptr_dtor(&zmessage_val);
      }
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      PHP_GRPC_MAKE_STD_ZVAL(recv_status);
      object_init(recv_status);
      recv_md = grpc_parse_metadata_array(&batch->recv_trailing_metadata);
      add_property_zval(recv_status, "metadata", recv_md);
      zval_ptr_dtor(recv_md);
      PHP_GRPC_FREE_STD_ZVAL(recv_md);
      PHP_GRPC_DELREF(array);
      add_property_long(recv_status, "code", batch->status);
      char *status_details_text = grpc_slice_to_c_string(batch->recv_status_details);
      php_grpc_add_property_string(recv_status, "details", status_details_text,
                                   true);
      gpr_free(status_details_text);
      add_property_zval(result, "status", recv_status);
      zval_ptr_dtor(recv_status);
      PHP_GRPC_DELREF(recv_status);
      PHP_GRPC_FREE_STD_ZVAL(recv_status);
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        add_property_bool(result, "cancelled", batch->cancelled);
        break;
      default:
        break;
    }
  }
}