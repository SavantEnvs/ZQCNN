// Fuzzes ZQ_CNN_Net::LoadFromBuffer: the .zqparams text parser (every layer's ReadParam),
// _check_connect, and the per-layer LoadBinary_NCHW(buffer, len) weight readers.
//
// Input layout: [flags: bit0 merge_bn, bit1 merge_prelu][u32 LE param_len][param text][weights, the rest].
// The Net::Forward kernels are not linked: mayhem/build.sh links the repo's own tripwire stubs
// (tools/zq_net_fwd_tripwires.h) that the load path never reaches.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ZQCNN/ZQ_CNN_Net.h"
#include "tools/zq_net_fwd_tripwires.h"
#include "tools/zq_concat_getsize_real.h"

// The library reports every rejected model on stdout; point stdout at a fixed in-memory sink and
// rewind it per input so that output costs nothing and never touches the filesystem.
static char g_sink[1 << 16];

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    FILE* f = fmemopen(g_sink, sizeof g_sink, "w");
    if (f) stdout = f;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fseek(stdout, 0, SEEK_SET);
    if (size < 5) return -1;
    const bool merge_bn = data[0] & 1, merge_prelu = (data[0] >> 1) & 1;
    uint32_t plen = (uint32_t)data[1] | ((uint32_t)data[2] << 8) | ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
    data += 5;
    size -= 5;
    if (plen > size) return -1;

    // Exact-sized heap copies so ASan sees any read past the stated lengths.
    std::vector<char> param(data, data + plen);
    std::vector<char> model(data + plen, data + size);
    const char* pp = param.empty() ? "" : param.data();
    const char* mp = model.empty() ? "" : model.data();

    ZQ::ZQ_CNN_Net net;
    net.LoadFromBuffer(pp, (__int64)param.size(), mp, (__int64)model.size(), merge_bn, 1e-12f, merge_prelu);
    return 0;
}
