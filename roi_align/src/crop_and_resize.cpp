#include <torch/extension.h>
//#include <TH/TH.h>
#include <stdio.h>
#include <math.h>

namespace torch {
void CropAndResizePerBox(
    const float * image_data, 
    const int batch_size,
    const int depth,
    //const int image_height,
    const int image_width,

    const float * boxes_data, 
    const int * box_index_data,
    const int start_box, 
    const int limit_box,

    float * corps_data,
    //const int crop_height,
    const int crop_width,
    const float extrapolation_value
) {
    const int image_channel_elements = image_width;
    const int image_elements = depth * image_channel_elements;

    const int channel_elements = crop_width;
    const int crop_elements = depth * channel_elements;

    int b;
    #pragma omp parallel for
    for (b = start_box; b < limit_box; ++b) {
        const float * box = boxes_data + b * 2;
        const float x1 = box[0];
        const float x2 = box[1];
        //const float y2 = box[2];
        //const float x2 = box[3];

        const int b_in = box_index_data[b];
        if (b_in < 0 || b_in >= batch_size) {
            printf("Error: batch_index %d out of range [0, %d)\n", b_in, batch_size);
            exit(-1);
        }
                const float width_scale =
            (crop_width > 1) ? (x2 - x1) * (image_width - 1) / (crop_width - 1)
                             : 0;

            for (int x = 0; x < crop_width; ++x)
            {
                const float in_x = (crop_width > 1)
                                       ? x1 * (image_width - 1) + x * width_scale
                                       : 0.5 * (x1 + x2) * (image_width - 1);
                
            
                const int left_x_index = floorf(in_x);
                const int right_x_index = ceilf(in_x);
                const float x_lerp = in_x - left_x_index;

                for (int d = 0; d < depth; ++d)
                {   
                    const float *pimage = image_data + b_in * image_elements + d * image_channel_elements;

                    const float top_left = pimage[left_x_index];
                    const float top_right = pimage[right_x_index];
                    
                    const float top = top_left + (top_right - top_left) * x_lerp;

                        
                    corps_data[crop_elements * b + channel_elements * d +  x] = top ;
                }
            }   // end for x
           // end for y
    }   // end for b

}


#define CHECK_CUDA(x) AT_ASSERTM(!x.type().is_cuda(), #x " must be a CPU tensor")
#define CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x " must be contiguous")
#define CHECK_DIMS(x) AT_ASSERTM(x.dim() == 3, #x " must have 3 dimensions")

#define CHECK_INPUT(x) CHECK_CUDA(x); CHECK_CONTIGUOUS(x)
#define CHECK_FLOAT(x) AT_ASSERTM(x.type().scalarType() == torch::ScalarType::Float, #x " must be float Tensor")
#define CHECK_INT(x) AT_ASSERTM(x.type().scalarType() == torch::ScalarType::Int, #x " must be int Tensor")

void crop_and_resize_forward(
    torch::Tensor image,     // (b,c,h,w)
    torch::Tensor boxes,      // [y1, x1, y2, x2]
    torch::Tensor box_index,    // range in [0, batch_size)
    const float extrapolation_value,
    //const int crop_height,
    const int crop_width,
    torch::Tensor crops
) {
    CHECK_INPUT(image);     CHECK_FLOAT(image);     CHECK_DIMS(image);
    CHECK_INPUT(boxes);     CHECK_FLOAT(boxes); //TODO: check dims for other arguments required.
    CHECK_INPUT(box_index); CHECK_INT(box_index);
    CHECK_INPUT(crops);     CHECK_FLOAT(crops);

    const int batch_size    = image.size(0);
    const int depth         = image.size(1);
    const int image_width   = image.size(2);

    const int num_boxes     = boxes.size(0);

    crops.resize_({num_boxes, depth, crop_width});
    crops.zero_();

    // crop_and_resize for each box
    CropAndResizePerBox(
        image.data<float>(),
        batch_size,
        depth,
        image_width,

        boxes.data<float>(),
        box_index.data<int>(),
        0,
        num_boxes,

        crops.data<float>(),
        crop_width,
        extrapolation_value
    );

}


void crop_and_resize_backward(
    torch::Tensor grads,
    torch::Tensor boxes,      // [y1, x1, y2, x2]
    torch::Tensor box_index,    // range in [0, batch_size)
    torch::Tensor grads_image // resize to [bsize, c, hc, wc]
) {
    CHECK_INPUT(grads);     CHECK_FLOAT(grads);
    CHECK_INPUT(boxes);     CHECK_FLOAT(boxes);
    CHECK_INPUT(box_index); CHECK_INT(box_index);
    CHECK_INPUT(grads_image); CHECK_FLOAT(grads_image); CHECK_DIMS(grads_image);

    // shape
    const int batch_size    = grads_image.size(0);
    const int depth         = grads_image.size(1);
    //nst int image_height  = grads_image.size(2);
    const int image_width   = grads_image.size(2);

    const int num_boxes     = grads.size(0);
    //nst int crop_height   = grads.size(2);
    const int crop_width    = grads.size(2);

    // n_elements
    const int image_channel_elements =  image_width;
    const int image_elements = depth * image_channel_elements;

    const int channel_elements = crop_width;
    const int crop_elements = depth * channel_elements;

    // init output space
    grads_image.zero_();
//    THFloatTensor_zero(grads_image);

    // data pointer
    const float * grads_data = grads.data<float>();
    const float * boxes_data = boxes.data<float>();
    const int * box_index_data = box_index.data<int>();
    float * grads_image_data = grads_image.data<float>();

    for (int b = 0; b < num_boxes; ++b) {
        const float * box = boxes_data + b * 2;
        const float x1 = box[0];
        const float x2 = box[1];

        const int b_in = box_index_data[b];
        if (b_in < 0 || b_in >= batch_size) {
            printf("Error: batch_index %d out of range [0, %d)\n", b_in, batch_size);
            exit(-1);
        }
        
        const float width_scale =
            (crop_width > 1) ? (x2 - x1) * (image_width - 1) / (crop_width - 1)
                             : 0;

        for (int x = 0; x < crop_width; ++x)
            {
                const float in_x = (crop_width > 1)
                                       ? x1 * (image_width - 1) + x * width_scale
                                       : 0.5 * (x1 + x2) * (image_width - 1);

                const int left_x_index = floorf(in_x);
                const int right_x_index = ceilf(in_x);
                const float x_lerp = in_x - left_x_index;

                for (int d = 0; d < depth; ++d)
                {
                    float *pimage = grads_image_data + b_in * image_elements + d * image_channel_elements;
                    const float grad_val = grads_data[crop_elements * b + channel_elements * d + x];

                    const float dtop =  grad_val;
                    pimage[left_x_index] += (1 - x_lerp) * dtop;
                    pimage[right_x_index] += x_lerp * dtop;

                }   // end d
            }   // end x
           // end y
    }   // end b
}



PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def(
      "forward",
      &torch::crop_and_resize_forward,
      "crop_and_resize_forward");
  m.def(
      "backward",
      &torch::crop_and_resize_backward,
      "crop_and_resize_forward");
}
}