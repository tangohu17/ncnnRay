#include "FaceDetector.h"

#include <algorithm>
//#include "omp.h"

Detector::Detector() :
        _nms(0.4),
        _threshold(0.6),
        _mean_val{104.f, 117.f, 123.f},
        _retinaface(false),
        Net(new ncnn::Net()) {
}

inline void Detector::Release() {
    if (Net != nullptr) {
        delete Net;
        Net = nullptr;
    }
}

Detector::Detector(const std::string &model_path, const ncnn::Option &opt, bool retinaface) :
        _nms(0.4),
        _threshold(0.6),
        _mean_val{104.f, 117.f, 123.f},
        _retinaface(retinaface),
        Net(new ncnn::Net()) {


#if NCNN_VULKAN
    Net->opt.use_vulkan_compute=true;
    if (Net->opt.use_vulkan_compute) {
        TraceLog(LOG_INFO, "ncnnRay: Detector opt using vulkan::%i", Net->opt.use_vulkan_compute);
        Net->set_vulkan_device(g_vkdev);
    }
#endif // NCNN_VULKAN


    std::string param = model_path + "/retinaface.param";
    std::string bin = model_path + "/retinaface.bin";

    Net->load_param(param.c_str());
    Net->load_model(bin.c_str());
    TraceLog(LOG_INFO, "ncnnRay: model loaded, GPU enabled?=%i", Net->opt.use_vulkan_compute);

}

void Detector::detectFaces(Image &img) {
//    TIMED_SCOPE( t, "Detector::detectFaces");
    ScopeTimer Tmr("Detector::detectFaces");
    ncnn::Mat in = rayImageToNcnn(img);
    cout << "Total:" << in.total() << endl;
    cout << "D:" << tensorDIMS(in) << endl;;

    vector <bbox> boxes;
    Detect(in, boxes);
    cout << "Face detections:" << boxes.size() << endl;;
    ImageDrawRectangle(&img, 5, 20, 20, 20, DARKPURPLE);

    for (int j = 0; j < boxes.size(); ++j) {
//        cout << "Iteration:" << j << endl;;
        auto face = boxes[j];
        Rectangle rect = {face.x1, face.y1, face.x2 - face.x1, face.y2 - face.y1};
        ImageDrawRectangleLines(&img, rect, 2, RED);
//        ImageDrawCircleV(&img, Vector2{(float) face.x1, (float) face.y1}, 5, BLUE);
    }

}

void Detector::Detect(ncnn::Mat &in, std::vector <bbox> &boxes) {
//    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR, bgr.cols, bgr.rows, bgr.cols, bgr.rows);

//    PERFORMANCE_CHECKPOINT_WITH_ID(timerFindSimilar, "Detector::Detect");
    in.substract_mean_normalize(_mean_val, 0);
    ncnn::Extractor ex = Net->create_extractor();
    ex.set_light_mode(true);
    ex.set_num_threads(4);
    ex.input(0, in);
    ncnn::Mat out, out1, out2;
    // loc
    ex.extract("output0", out);
    // class
    ex.extract("530", out1);
    //landmark
    ex.extract("529", out2);
    std::vector <box> anchor;
    if (_retinaface)
        create_anchor_retinaface(anchor, in.w, in.h);
    else
        create_anchor(anchor, in.w, in.h);

    std::vector <bbox> total_box;
    float *ptr = out.channel(0);
    float *ptr1 = out1.channel(0);
    float *landms = out2.channel(0);

    // #pragma omp parallel for num_threads(2)
    for (int i = 0; i < anchor.size(); ++i) {
        if (*(ptr1 + 1) > _threshold) {
            box tmp = anchor[i];
            box tmp1;
            bbox result;

            // loc and conf
            tmp1.cx = tmp.cx + *ptr * 0.1 * tmp.sx;
            tmp1.cy = tmp.cy + *(ptr + 1) * 0.1 * tmp.sy;
            tmp1.sx = tmp.sx * exp(*(ptr + 2) * 0.2);
            tmp1.sy = tmp.sy * exp(*(ptr + 3) * 0.2);

            result.x1 = (tmp1.cx - tmp1.sx / 2) * in.w;
            if (result.x1 < 0)
                result.x1 = 0;
            result.y1 = (tmp1.cy - tmp1.sy / 2) * in.h;
            if (result.y1 < 0)
                result.y1 = 0;
            result.x2 = (tmp1.cx + tmp1.sx / 2) * in.w;
            if (result.x2 > in.w)
                result.x2 = in.w;
            result.y2 = (tmp1.cy + tmp1.sy / 2) * in.h;
            if (result.y2 > in.h)
                result.y2 = in.h;
            result.s = *(ptr1 + 1);

            // landmark
            for (int j = 0; j < 5; ++j) {
                result.point[j]._x = (tmp.cx + *(landms + (j << 1)) * 0.1 * tmp.sx) * in.w;
                result.point[j]._y = (tmp.cy + *(landms + (j << 1) + 1) * 0.1 * tmp.sy) * in.h;
            }

            total_box.push_back(result);
        }
        ptr += 4;
        ptr1 += 2;
        landms += 10;
    }

    std::sort(total_box.begin(), total_box.end(), cmp);
    nms(total_box, _nms);
    printf("%d\n", (int) total_box.size());

    for (int j = 0; j < total_box.size(); ++j) {
        boxes.push_back(total_box[j]);
    }
}

inline bool Detector::cmp(bbox a, bbox b) {
    if (a.s > b.s)
        return true;
    return false;
}

inline void Detector::SetDefaultParams() {
    _nms = 0.4;
    _threshold = 0.6;
    _mean_val[0] = 104;
    _mean_val[1] = 117;
    _mean_val[2] = 123;
    Net = nullptr;

}

Detector::~Detector() {
    Release();
}

void Detector::create_anchor(std::vector <box> &anchor, int w, int h) {
//    anchor.reserve(num_boxes);
    anchor.clear();
    std::vector <std::vector<int>> feature_map(4), min_sizes(4);
    float steps[] = {8, 16, 32, 64};
    for (int i = 0; i < feature_map.size(); ++i) {
        feature_map[i].push_back(ceil(h / steps[i]));
        feature_map[i].push_back(ceil(w / steps[i]));
    }
    std::vector<int> minsize1 = {10, 16, 24};
    min_sizes[0] = minsize1;
    std::vector<int> minsize2 = {32, 48};
    min_sizes[1] = minsize2;
    std::vector<int> minsize3 = {64, 96};
    min_sizes[2] = minsize3;
    std::vector<int> minsize4 = {128, 192, 256};
    min_sizes[3] = minsize4;


    for (int k = 0; k < feature_map.size(); ++k) {
        std::vector<int> min_size = min_sizes[k];
        for (int i = 0; i < feature_map[k][0]; ++i) {
            for (int j = 0; j < feature_map[k][1]; ++j) {
                for (int l = 0; l < min_size.size(); ++l) {
                    float s_kx = min_size[l] * 1.0 / w;
                    float s_ky = min_size[l] * 1.0 / h;
                    float cx = (j + 0.5) * steps[k] / w;
                    float cy = (i + 0.5) * steps[k] / h;
                    box axil = {cx, cy, s_kx, s_ky};
                    anchor.push_back(axil);
                }
            }
        }

    }

}

void Detector::create_anchor_retinaface(std::vector <box> &anchor, int w, int h) {
//    anchor.reserve(num_boxes);
    anchor.clear();
    std::vector <std::vector<int>> feature_map(3), min_sizes(3);
    float steps[] = {8, 16, 32};
    for (int i = 0; i < feature_map.size(); ++i) {
        feature_map[i].push_back(ceil(h / steps[i]));
        feature_map[i].push_back(ceil(w / steps[i]));
    }
    std::vector<int> minsize1 = {10, 20};
    min_sizes[0] = minsize1;
    std::vector<int> minsize2 = {32, 64};
    min_sizes[1] = minsize2;
    std::vector<int> minsize3 = {128, 256};
    min_sizes[2] = minsize3;

    for (int k = 0; k < feature_map.size(); ++k) {
        std::vector<int> min_size = min_sizes[k];
        for (int i = 0; i < feature_map[k][0]; ++i) {
            for (int j = 0; j < feature_map[k][1]; ++j) {
                for (int l = 0; l < min_size.size(); ++l) {
                    float s_kx = min_size[l] * 1.0 / w;
                    float s_ky = min_size[l] * 1.0 / h;
                    float cx = (j + 0.5) * steps[k] / w;
                    float cy = (i + 0.5) * steps[k] / h;
                    box axil = {cx, cy, s_kx, s_ky};
                    anchor.push_back(axil);
                }
            }
        }

    }

}

void Detector::nms(std::vector <bbox> &input_boxes, float NMS_THRESH) {
    std::vector<float> vArea(input_boxes.size());
    for (int i = 0; i < int(input_boxes.size()); ++i) {
        vArea[i] = (input_boxes.at(i).x2 - input_boxes.at(i).x1 + 1)
                   * (input_boxes.at(i).y2 - input_boxes.at(i).y1 + 1);
    }
    for (int i = 0; i < int(input_boxes.size()); ++i) {
        for (int j = i + 1; j < int(input_boxes.size());) {
            float xx1 = std::max(input_boxes[i].x1, input_boxes[j].x1);
            float yy1 = std::max(input_boxes[i].y1, input_boxes[j].y1);
            float xx2 = std::min(input_boxes[i].x2, input_boxes[j].x2);
            float yy2 = std::min(input_boxes[i].y2, input_boxes[j].y2);
            float w = std::max(float(0), xx2 - xx1 + 1);
            float h = std::max(float(0), yy2 - yy1 + 1);
            float inter = w * h;
            float ovr = inter / (vArea[i] + vArea[j] - inter);
            if (ovr >= NMS_THRESH) {
                input_boxes.erase(input_boxes.begin() + j);
                vArea.erase(vArea.begin() + j);
            } else {
                j++;
            }
        }
    }
}
