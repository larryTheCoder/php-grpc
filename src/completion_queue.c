/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "completion_queue.h"
#include "batch.h"
#include "php_grpc.h"

void grpc_php_init_completion_queue(TSRMLS_D) {
  GRPC_G(storage).completion_queue = grpc_completion_queue_create_for_pluck(NULL);
}

void grpc_php_shutdown_completion_queue(TSRMLS_D) {
  grpc_completion_queue_shutdown(GRPC_G(storage).completion_queue);
  grpc_completion_queue_destroy(GRPC_G(storage).completion_queue);
}

void grpc_php_init_next_queue(TSRMLS_D) {
  GRPC_G(storage).next_queue = grpc_completion_queue_create_for_next(NULL);
  GRPC_G(storage).pending_batches = 0;
  GRPC_G(storage).draining_next_queue = false;
}

bool grpc_php_drain_next_queue(bool shutdown, gpr_timespec deadline TSRMLS_DC) {
  grpc_event event;
  zval params[1];
  zval retval;
  if (GRPC_G(storage).draining_next_queue) {
    return true;
  }
  if (GRPC_G(storage).pending_batches == 0) {
    return false;
  }
  GRPC_G(storage).draining_next_queue = true;
  do {
    event = grpc_completion_queue_next(GRPC_G(storage).next_queue, deadline, NULL);
    if (event.type == GRPC_OP_COMPLETE) {
      struct batch *batch = (struct batch *) event.tag;

      if (!shutdown) {
        if (event.success != 0) {
          batch_consume(batch, &params[0]);
        } else {
          ZVAL_NULL(&params[0]);
        }

        batch->fci.param_count = 1;
        batch->fci.params = params;
        batch->fci.retval = &retval;

        zend_call_function(&batch->fci, &batch->fcc);

        for (int i = 0; i < batch->fci.param_count; i++) {
          zval_dtor(&params[i]);
        }
        zval_dtor(&retval);
      }

      batch_destroy(batch);
      GRPC_G(storage).pending_batches--;
      if (GRPC_G(storage).pending_batches == 0) {
        break;
      }
    }
  } while (event.type != GRPC_QUEUE_TIMEOUT);
  GRPC_G(storage).draining_next_queue = false;

  return GRPC_G(storage).pending_batches > 0 ? true : false;
}

void grpc_php_shutdown_next_queue(TSRMLS_D) {
  while (grpc_php_drain_next_queue(true, gpr_inf_future(GPR_CLOCK_MONOTONIC) TSRMLS_CC));
  grpc_completion_queue_shutdown(GRPC_G(storage).next_queue);
  grpc_completion_queue_destroy(GRPC_G(storage).next_queue);
}
