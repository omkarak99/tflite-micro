/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/lite/kernels/internal/reference/floor.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"

namespace tflite {
namespace ops {
namespace micro {
namespace floor {

constexpr int kInputTensor = 0;
constexpr int kOutputTensor = 0;

TfLiteStatus Eval(TfLiteContext* context, TfLiteNode* node) {
  const TfLiteEvalTensor* input =
      tflite::micro::GetEvalInput(context, node, kInputTensor);
  TF_LITE_ENSURE_TYPES_EQ(context, input->type, kTfLiteFloat32);
  TfLiteEvalTensor* output =
      tflite::micro::GetEvalOutput(context, node, kOutputTensor);
#if HIFI_VFPU
  int err;
  const float* inp_data_ptr;
  float* out_data_ptr;
  const RuntimeShape& input_shape = tflite::micro::GetTensorShape(input);
  const RuntimeShape& output_shape = tflite::micro::GetTensorShape(output);
  const int flat_size = MatchingFlatSize(input_shape, output_shape);

  inp_data_ptr = tflite::micro::GetTensorData<float>(input);
  out_data_ptr = tflite::micro::GetTensorData<float>(output);

  err = xa_nn_elm_floor_f32_f32(out_data_ptr, inp_data_ptr, flat_size);

  TF_LITE_ENSURE(context, err == 0);
#else
  reference_ops::Floor(tflite::micro::GetTensorShape(input),
                       tflite::micro::GetTensorData<float>(input),
                       tflite::micro::GetTensorShape(output),
                       tflite::micro::GetTensorData<float>(output));
#endif // HIFI_VFPU
  return kTfLiteOk;
}
}  // namespace floor

TfLiteRegistration Register_FLOOR() {
  return {/*init=*/nullptr,
          /*free=*/nullptr,
          /*prepare=*/nullptr,
          /*invoke=*/floor::Eval,
          /*profiling_string=*/nullptr,
          /*builtin_code=*/0,
          /*custom_name=*/nullptr,
          /*version=*/0};
}

}  // namespace micro
}  // namespace ops
}  // namespace tflite