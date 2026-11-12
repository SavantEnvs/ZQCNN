// Fuzzes ZQ_CNN_Tensor4D (all three NHW_C alignment variants): BGR/gray image import, compact-NCHW import,
// Padding, ROI, nearest/bilinear resize, flips and Tile with attacker-chosen geometry.
//
// Argument preconditions: Padding widths and rect sizes are non-negative, and resize destination sizes are
// >= 1. A zero destination width/height makes ChangeSize() produce an empty tensor with a null data pointer,
// and the resize kernels then do `null_ptr += 0` (clang UBSan pointer-overflow, no memory effect); that is a
// degenerate empty-output case, not a memory-safety finding. The library's own ROI() rejects
// width/height < 0 ("顺带把 width<0 / height<0 也拒掉", ZQ_CNN_Tensor4D.h) and ChangeSize clamps negative
// borders to 0 while Padding() stores them, so negative values are API misuse, not input to find bugs with.
// Negative rect offsets stay in: the library rejects them (off_x/off_y < 0).
//
// Input: [op|kind<<4][W-1][H-1][6 parameter bytes][pixel bytes, zero-extended to the needed size].
#include <cstdint>
#include <cstring>
#include <vector>
#include "ZQCNN/ZQ_CNN_Tensor4D.h"

using namespace ZQ;

static int sgn(uint8_t b, int lo, int span) { return lo + (int)(b % span); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 9) return -1;
    const int op = data[0] & 15, kind = (data[0] >> 4) % 3;
    const int W = 1 + data[1] % 48, H = 1 + data[2] % 48;
    const uint8_t* p = data + 3;
    const uint8_t* rest = data + 9;
    const size_t rest_len = size - 9;

    ZQ_CNN_Tensor4D_NHW_C_Align0 t0;
    ZQ_CNN_Tensor4D_NHW_C_Align128bit t1;
    ZQ_CNN_Tensor4D_NHW_C_Align256bit t2;
    ZQ_CNN_Tensor4D* t = kind == 0 ? (ZQ_CNN_Tensor4D*)&t0 : kind == 1 ? (ZQ_CNN_Tensor4D*)&t1 : (ZQ_CNN_Tensor4D*)&t2;
    ZQ_CNN_Tensor4D_NHW_C_Align0 d0;
    ZQ_CNN_Tensor4D_NHW_C_Align128bit d1;
    ZQ_CNN_Tensor4D_NHW_C_Align256bit d2;
    ZQ_CNN_Tensor4D* dst = (p[5] & 1) ? (ZQ_CNN_Tensor4D*)&d1 : (p[5] & 2) ? (ZQ_CNN_Tensor4D*)&d2 : (ZQ_CNN_Tensor4D*)&d0;

    // Exact-sized pixel buffer so ASan sees reads past the image.
    const int step = W * 3 + (p[4] % 4);
    std::vector<unsigned char> img((size_t)step * H, 0);
    memcpy(img.data(), rest, rest_len < img.size() ? rest_len : img.size());

    switch (op % 12) {
    case 0:
        if (t->ConvertFromBGR(img.data(), W, H, step)) t->Padding(sgn(p[0], 0, 12), sgn(p[1], 0, 12), p[2] & 3);
        break;
    case 1:
        if (t->ConvertFromBGR2GRAY(img.data(), W, H, step)) { t->FlipX(); t->FlipY(); }
        break;
    case 2:
        if (t->ConvertFromGray(img.data(), W, H, W + (p[4] % 4))) t->Padding(sgn(p[0], 0, 6), sgn(p[1], 0, 6), sgn(p[2], 0, 6), sgn(p[3], 0, 6), p[4] & 3);
        break;
    case 3:
        if (t->ConvertFromBGR(img.data(), W, H, step))
            t->ROI(*dst, sgn(p[0], -4, 60), sgn(p[1], -4, 60), sgn(p[2], -4, 60), sgn(p[3], -4, 60), p[4] % 3, p[5] % 3);
        break;
    case 4:
        if (t->ConvertFromBGR(img.data(), W, H, step))
            t->ResizeBilinear(*dst, sgn(p[0], 1, 100), sgn(p[1], 1, 100), p[2] % 3, p[3] % 3, (ZQ_CNN_Tensor4D::SAMPLE_ALIGN_TYPE)(p[4] & 1));
        break;
    case 5:
        if (t->ConvertFromBGR(img.data(), W, H, step))
            t->ResizeNearest(*dst, sgn(p[0], 1, 100), sgn(p[1], 1, 100), p[2] % 3, p[3] % 3, (ZQ_CNN_Tensor4D::SAMPLE_ALIGN_TYPE)(p[4] & 1));
        break;
    case 6:
        if (t->ConvertFromBGR(img.data(), W, H, step))
            t->ResizeBilinearRect(*dst, sgn(p[0], 1, 80), sgn(p[1], 1, 80), p[2] % 3, p[3] % 3,
                                  sgn(p[4], -3, 40), sgn(p[5], -3, 40), sgn(p[0] ^ p[4], 0, 60), sgn(p[1] ^ p[5], 0, 60));
        break;
    case 7:
        if (t->ConvertFromBGR(img.data(), W, H, step))
            t->ResizeNearestRect(*dst, sgn(p[0], 1, 80), sgn(p[1], 1, 80), p[2] % 3, p[3] % 3,
                                 sgn(p[4], -3, 40), sgn(p[5], -3, 40), sgn(p[0] ^ p[4], 0, 60), sgn(p[1] ^ p[5], 0, 60));
        break;
    case 8:
    case 9: {
        const int n = 1 + p[0] % 2, c = 1 + p[1] % 9;
        const int hh = 1 + H % 16, ww = 1 + W % 16;
        std::vector<float> buf((size_t)n * c * hh * ww, 0.f);
        for (size_t i = 0; i < buf.size() && i < rest_len; i++) buf[i] = (float)rest[i] - 128.f;
        if (t->ConvertFromCompactNCHW(buf.data(), n, c, hh, ww, p[2] % 3, p[3] % 3)) {
            t->Tile(*dst, sgn(p[4], -2, 5), sgn(p[5], -2, 5), sgn(p[0] ^ p[1], -2, 5), sgn(p[2] ^ p[3], -2, 5));
            std::vector<float> out((size_t)n * c * hh * ww);
            t->ConvertToCompactNCHW(out.data());
        }
        break;
    }
    case 10:
        if (t->ConvertFromBGR(img.data(), W, H, step)) {
            t->MulScalar(0.5f);
            t->AddScalar(1.f);
            t->ChangeSize(1 + p[0] % 3, sgn(p[1], -1, 40), sgn(p[2], -1, 40), sgn(p[3], -1, 20), p[4] % 3, p[5] % 3);
        }
        break;
    case 11:
        if (t->ConvertFromBGR(img.data(), W, H, step)) {
            t->Padding(p[0] % 4, p[1] % 4, 0);
            t->Padding(p[2] % 6, p[3] % 6, p[4] % 6, p[5] % 6, 0);
            t->ShrinkToFit();
        }
        break;
    }
    return 0;
}
