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

grpc_completion_queue *completion_queue;
grpc_completion_queue *next_queue;
int pending_batches;
bool draining_next_queue;

void grpc_php_init_completion_queue(TSRMLS_D) {
  completion_queue = grpc_completion_queue_create_for_pluck(NULL);
}

void grpc_php_shutdown_completion_queue(TSRMLS_D) {
  grpc_completion_queue_shutdown(completion_queue);
  grpc_completion_queue_destroy(completion_queue);
}

void grpc_php_init_next_queue(TSRMLS_D) {
  next_queue = grpc_completion_queue_create_for_next(NULL);
  pending_batches = 0;
  draining_next_queue = false;
}

bool grpc_php_drain_next_queue(bool shutdown, gpr_timespec deadline TSRMLS_DC) {
  grpc_event event;
  zval params[2];
  zval retval;
  if (draining_next_queue) {
    return true;
  }
  if (pending_batches == 0) {
    return false;
  }
  draining_next_queue = true;
  do {
    event = grpc_completion_queue_next(next_queue, deadline, NULL);
    if (event.type == GRPC_OP_COMPLETE) {
      struct batch *batch = (struct batch*) event.tag;

      if (!shutdown) {
        if (event.success) {
          ZVAL_NULL(&params[0]);
          batch_consume(batch, &params[1]);
          batch->fci.param_count = 2;
        } else {
          ZVAL_STRING(&params[0], "The async function encountered an error");
          batch->fci.param_count = 1;
        }
        batch->fci.params = params;
        batch->fci.retval = &retval;

        zend_call_function(&batch->fci, &batch->fcc);

        for (int i = 0; i < batch->fci.param_count; i++) {
          zval_dtor(&params[i]);
        }
        zval_dtor(&retval);
      }

      batch_destroy(batch);
      pending_batches--;
      if (pending_batches == 0) {
        break;
      }
    }
  } while (event.type != GRPC_QUEUE_TIMEOUT);
  draining_next_queue = false;

  return pending_batches > 0 ? true : false;
}

void grpc_php_shutdown_next_queue(TSRMLS_D) {
  while (grpc_php_drain_next_queue(true, gpr_inf_future(GPR_CLOCK_MONOTONIC) TSRMLS_CC));
  grpc_completion_queue_shutdown(next_queue);
  grpc_completion_queue_destroy(next_queue);
}
