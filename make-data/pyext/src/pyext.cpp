/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../include/pyext.h"
#include <omp.h>

using namespace std;

void makeJPEG(PyObject* _py_list_src, int idx, int _target_size, bool _crop_to_square, PyObject* _py_list_tgt); 

static PyMethodDef _MakeDataPyExtMethods[] = {{ "resizeJPEG", resizeJPEG, METH_VARARGS },
                                              { NULL, NULL }
};

void init_MakeDataPyExt() {
    (void) Py_InitModule("_MakeDataPyExt", _MakeDataPyExtMethods);
}

PyObject* resizeJPEG(PyObject *self, PyObject *args) {

    PyListObject* pyListSrc;
    int tgtImgSize, numThreads;
    int cropToSquare;

    if (!PyArg_ParseTuple(args, "O!iii",
                          &PyList_Type, &pyListSrc,
                          &tgtImgSize,
                          &numThreads,
                          &cropToSquare)) {
        return NULL;
    }

    int num_imgs = PyList_GET_SIZE(pyListSrc);
    PyObject* pyListTgt = PyList_New(0);
    omp_set_num_threads(numThreads);
    #pragma omp parallel for
    for (int t = 0; t < num_imgs; ++t) {
        makeJPEG((PyObject*)pyListSrc, t, tgtImgSize, cropToSquare, pyListTgt);
    }

    return pyListTgt;
}
void makeJPEG(PyObject* _py_list_src, int idx, int _target_size, bool _crop_to_square, PyObject* _py_list_tgt) {
    cv::Mat _resized_mat_buffer;
    cv::cuda::GpuMat _resized_mat_buffer_gpu;
    std::vector<uchar> _output_jpeg_buffer; 
    std::vector<int> _encode_params;
    _encode_params.push_back(CV_IMWRITE_JPEG_QUALITY);
    _encode_params.push_back(JPEG_QUALITY);

    /*
     * Decompress JPEG
     */
    PyObject* pySrc = PyList_GET_ITEM(_py_list_src, idx);
    uchar* src = (unsigned char*)PyString_AsString(pySrc);
    size_t src_len = PyString_GET_SIZE(pySrc);
    vector<uchar> src_vec(src, src + src_len);

    cv::Mat decoded_mat = cv::imdecode(cv::Mat(src_vec), CV_LOAD_IMAGE_COLOR);
    assert(decoded_mat.channels() == 3);

    // Load to GPU.
    cv::cuda::GpuMat decoded_mat_gpu;
    decoded_mat_gpu.upload(decoded_mat);

    /*
     * Resize
     */
    double min_dim = std::min(decoded_mat.size().height, decoded_mat.size().width);
    double scale_factor = _target_size / min_dim;

    int new_height = round(scale_factor * decoded_mat.size().height);
    int new_width = round(scale_factor * decoded_mat.size().width);
    assert((new_height == _target_size && new_width >= _target_size)
           || (new_width == _target_size && new_height >= _target_size));
    int interpolation = scale_factor == 1 ? cv::INTER_LINEAR
                      : scale_factor > 1 ? cv::INTER_CUBIC : cv::INTER_AREA;

    cv::cuda::resize(decoded_mat_gpu, _resized_mat_buffer_gpu, cv::Size(new_width, new_height), 0, 0, interpolation);
    _resized_mat_buffer_gpu.download(_resized_mat_buffer);

    /*
     * Conditionally crop and compress JPEG
     */
    if (_crop_to_square) {
        int crop_start_x = (new_width - _target_size) / 2;
        int crop_start_y = (new_height - _target_size) / 2;
        cv::Rect cropRect(crop_start_x, crop_start_y, _target_size, _target_size);
        cv::Mat cropped_mat_buffer = _resized_mat_buffer(cropRect);
        cv::imencode(".jpg", cropped_mat_buffer, _output_jpeg_buffer, _encode_params);
    } else {
        cv::imencode(".jpg", _resized_mat_buffer, _output_jpeg_buffer, _encode_params);
    }

    char* output_jpeg_buffer_ptr = reinterpret_cast<char*>(&_output_jpeg_buffer[0]);
    PyObject* pyStr = PyString_FromStringAndSize(output_jpeg_buffer_ptr, _output_jpeg_buffer.size());
    #pragma omp critical
    {
        PyList_Append(_py_list_tgt, pyStr);
    }
    Py_DECREF(pyStr);
}

