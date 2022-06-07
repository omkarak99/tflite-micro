/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/micro/kernels/circular_buffer.h"

#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/compatibility.h"
#include "tensorflow/lite/kernels/internal/quantization_util.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/kernels/op_macros.h"
#include "tensorflow/lite/micro/flatbuffer_utils.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"

/*
 * The circular buffer custom operator is used to implement strided streaming
 * convolutions on TFLite Micro.  Each time this operator is invoked, it checks
 * whether or not to run, based on a predetermined stride in time.  If the op
 * runs, it inserts the input into the end of the output buffer and shifts the
 * output values towards the start of the buffer.  It discards the oldest value
 * in the output buffer.
 *
 * Input: [<input N+1]
 * Before shifting:
 * Output: [<input 1>, <input 2>, <input ...>, <input N>]
 *
 * After shifting:
 * Output: [<input 2>, <input 3>, <input ...>, <input N+1>]
 *
 * We make some assumptions in this custom operator:
 * - Input shape must be [1, 1, 1, depth]
 * - Output shape must be [1, num_slots, 1, depth]
 * - Input and output types must match.
 * - Input and output quantization params must be identical.
 */
namespace tflite {

void* CircularBufferInit(TfLiteContext* context, const char* buffer,
                         size_t length) {
  TFLITE_DCHECK(context->AllocatePersistentBuffer != nullptr);
  OpDataCircularBuffer* op_data = static_cast<OpDataCircularBuffer*>(
      context->AllocatePersistentBuffer(context, sizeof(OpDataCircularBuffer)));

  if (buffer != nullptr && length > 0) {
    const uint8_t* buffer_t = reinterpret_cast<const uint8_t*>(buffer);
    tflite::FlexbufferWrapper wrapper(buffer_t, length);
    op_data->cycles_max = wrapper.ElementAsInt32(kCircularBufferCyclesMaxIndex);
  } else {
    op_data->cycles_max = 0;
  }

  return op_data;
}

// Shifts buffer over by the output depth, and write new input to end of buffer.
// num_slots is the number of samples stored in the output buffer.
// depth is the size of each sample.
void EvalInt8(const int8_t* input, int num_slots, int depth, int8_t* output) {
  memmove(output, &output[depth], (num_slots - 1) * depth);
  memcpy(&output[(num_slots - 1) * depth], input, depth);
}

TfLiteStatus CircularBufferEval(TfLiteContext* context, TfLiteNode* node) {
  const TfLiteEvalTensor* input =
      tflite::micro::GetEvalInput(context, node, kCircularBufferInputTensor);
  TfLiteEvalTensor* output =
      tflite::micro::GetEvalOutput(context, node, kCircularBufferOutputTensor);

  TFLITE_DCHECK(node->user_data != nullptr);
  OpDataCircularBuffer* data =
      reinterpret_cast<OpDataCircularBuffer*>(node->user_data);

  int num_slots = output->dims->data[1];
  int depth = output->dims->data[2] * output->dims->data[3];

  if (input->type == kTfLiteInt8) {
#if defined(HIFI5) || defined(HIFI4)
    const int8_t* xa_input;
    int8_t* xa_output;
    int err;
    xa_input = tflite::micro::GetTensorData<int8_t>(input);
    xa_output =tflite::micro::GetTensorData<int8_t>(output);
    err = xa_nn_memmove_8_8(xa_output, &xa_output[depth], (num_slots-1)*depth);
    TF_LITE_ENSURE(context, (err==0) );
    memcpy(&xa_output[(num_slots - 1) * depth], xa_input, depth);
#else
    EvalInt8(tflite::micro::GetTensorData<int8_t>(input), num_slots, depth,
             tflite::micro::GetTensorData<int8_t>(output));
#endif // defined(HIFI5) || defined(HIFI4)
  } else {
    TF_LITE_KERNEL_LOG(context, "Type %s (%d) not supported.",
                       TfLiteTypeGetName(input->type), input->type);
    return kTfLiteError;
  }

  if (--data->cycles_until_run != 0) {
    // Signal the interpreter to end current run if the delay before op invoke
    // has not been reached.
    // TODO(b/149795762): Add kTfLiteAbort to TfLiteStatus enum.
    return static_cast<TfLiteStatus>(kTfLiteAbort);
  }

  data->cycles_until_run = data->cycles_max;

  return kTfLiteOk;
}

TfLiteRegistration* Register_CIRCULAR_BUFFER() {
  static TfLiteRegistration r = tflite::micro::RegisterOp(CircularBufferInit, CircularBufferPrepare, CircularBufferEval);
  return &r;
}

}  // namespace tflite