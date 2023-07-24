#ifndef NET_GRPC_PHP_GRPC_BATCH_H_
#define NET_GRPC_PHP_GRPC_BATCH_H_

#include <grpc/grpc.h>
#include <zend_API.h>

struct batch {
  grpc_call *wrapped;
  zend_fcall_info fci;
  zend_fcall_info_cache fcc;

  grpc_op ops[8];
  size_t op_num;

  grpc_metadata_array metadata;
  grpc_metadata_array trailing_metadata;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  grpc_status_code status;
  grpc_slice recv_status_details;
  grpc_slice send_status_details;
  grpc_byte_buffer *message;
  int cancelled;
};

struct batch* batch_new(grpc_call *wrapped);
void batch_consume(struct batch* batch, zval *result);
void batch_destroy(struct batch* batch);

#endif /* NET_GRPC_PHP_GRPC_BATCH_H_ */